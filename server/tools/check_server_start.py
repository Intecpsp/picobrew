#!/usr/bin/env python3
"""Build/start smoke in a disposable workspace; Python is required only for tests.

Run explicitly with ``python tools/check_server_start.py`` from server/. The
default builds with Go and runs the native binary twice. --make exercises the
repository/component Make targets and a standalone server copy. Every child
has a restricted PATH without Node/npm; fixtures contain no web/ source tree.
"""
import argparse
import ctypes
import http.client
import http.cookiejar
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import socket
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.request


SOURCE = Path(__file__).resolve().parents[1]
BASE = 'http://127.0.0.1:8080'
WINDOWS = os.name == 'nt'


def free_port():
    if not listener_closed():
        raise RuntimeError('Port 8080 is already in use; this test will not contact an existing server.')


def listener_closed():
    try:
        with socket.create_connection(('127.0.0.1', 8080), timeout=.2):
            return False
    except ConnectionRefusedError:
        return True


def ensure_windows_console():
    if not WINDOWS:
        return
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    if not kernel.GetConsoleCP() and not kernel.AllocConsole():
        raise ctypes.WinError(ctypes.get_last_error())


def start(command, directory, env, output):
    options = {'creationflags': subprocess.CREATE_NEW_PROCESS_GROUP} if WINDOWS else {'start_new_session': True}
    return subprocess.Popen(command, cwd=directory, env=env, stdin=subprocess.DEVNULL,
                            stdout=output, stderr=subprocess.STDOUT, **options)


def terminal_interrupt(process):
    if WINDOWS:
        # Emit a real console event, confined to this build/server process group.
        # TerminateProcess would bypass the Go server's graceful cleanup.
        process.send_signal(signal.CTRL_BREAK_EVENT)
    else:
        os.killpg(process.pid, signal.SIGINT)


def cleanup(process):
    if process is None:
        return
    try:
        if process.poll() is None:
            terminal_interrupt(process)
            process.wait(timeout=5)
    except (OSError, subprocess.TimeoutExpired):
        if WINDOWS:
            subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        else:
            try:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=3)
            except ProcessLookupError:
                pass
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=5)


def api(opener, path, body=None, csrf=None):
    headers = {}
    if body is not None:
        body = json.dumps(body).encode('utf-8')
        headers['Content-Type'] = 'application/json'
    if csrf:
        headers['X-CSRF-Token'] = csrf
    request = urllib.request.Request(BASE + path, data=body, headers=headers)
    with opener.open(request, timeout=3) as response:
        return response.status, json.load(response)


def stream(jar):
    connection = http.client.HTTPConnection('127.0.0.1', 8080, timeout=5)
    cookie = '; '.join(item.name + '=' + item.value for item in jar)
    connection.request('GET', '/api/v1/events', headers={'Cookie': cookie, 'Accept': 'text/event-stream'})
    response = connection.getresponse()
    assert response.status == 200, response.status
    assert response.getheader('Content-Type') == 'text/event-stream'
    event = b''
    while not event.endswith(b'\n\n'):
        line = response.readline()
        assert line, 'SSE ended before its first event'
        event += line
    assert b'event: fleet' in event
    complete = threading.Event()
    state = {}

    def drain():
        try:
            response.read()
            state['clean_eof'] = True
        except Exception as error:
            state['error'] = str(error)
        finally:
            complete.set()

    reader = threading.Thread(target=drain, daemon=True)
    reader.start()
    return connection, response, reader, complete, state


def copy_sources(destination, *, include_root=True):
    server = destination / 'server'
    server.mkdir(parents=True)
    for name in ('go.mod', 'go.sum', 'Makefile'):
        shutil.copy2(SOURCE / name, server / name)
    for name in ('cmd', 'internal'):
        shutil.copytree(SOURCE / name, server / name,
                        ignore=shutil.ignore_patterns('__pycache__', '.pytest_cache'))
    root_makefile = SOURCE.parent / 'Makefile'
    if include_root and root_makefile.is_file():
        shutil.copy2(root_makefile, destination / 'Makefile')
    assert not (server / 'web').exists()
    assert not (server / 'tools').exists()
    return server


def go_only_environment(folder, env, selected_go):
    """Make Node/npm unavailable, even on a developer host that has them installed."""
    goroot = Path(subprocess.check_output([selected_go, 'env', 'GOROOT'], cwd=SOURCE,
                                         env=env, text=True).strip()).resolve()
    native_go = goroot / 'bin' / ('go.exe' if WINDOWS else 'go')
    tool_bin = folder / 'Go tools without Node'
    tool_bin.mkdir()
    go = tool_bin / native_go.name
    shutil.copy2(native_go, go)
    paths = [str(tool_bin)]
    if WINDOWS:
        system_root = env.get('SYSTEMROOT') or env.get('SystemRoot')
        if not system_root:
            raise RuntimeError('Windows SystemRoot is required for native system tools.')
        paths += [str(Path(system_root) / 'System32'), system_root]
    result = {key: value for key, value in env.items()
              if key.upper() not in ('PATH', 'NODE', 'NPM', 'GOROOT', 'GO', 'SHELL', 'MAKESHELL')}
    result.update({'PATH': os.pathsep.join(paths), 'GOROOT': str(goroot), 'GO': str(go),
                   'CGO_ENABLED': '0'})
    if WINDOWS:
        # Native Windows GNU Make should use the OS command interpreter, not
        # a developer's optional Git/MSYS shell outside this restricted PATH.
        result['MAKESHELL'] = str(Path(system_root) / 'System32/cmd.exe')
    for name in ('node', 'node.exe', 'npm', 'npm.cmd', 'npx', 'npx.cmd'):
        assert shutil.which(name, path=result['PATH']) is None, f'{name} remains available on PATH'
    print('Dependency isolation verified: no Node/npm on PATH; no web/ or tooling in build fixtures.', flush=True)
    return result, str(go)


def failed_build_does_not_start(server, env, make):
    """A real Go compile error must not launch the previously built executable."""
    failure = server / 'cmd/picobrew-hub/zz_intentional_smoke_failure.go'
    failure.write_text('package main\nvar smokeBuildFailure = intentionallyUndefinedBuildSymbol\n', encoding='utf-8')
    process = None
    free_port()
    try:
        with tempfile.TemporaryFile() as log:
            process = start([make, 'server-start'], server, env, log)
            result = process.wait(timeout=20)
            assert result != 0, 'An invalid Go build unexpectedly succeeded'
            assert listener_closed(), 'Failed build launched the stale server executable'
    finally:
        cleanup(process)
        failure.unlink()
    print('Go build failure correctly prevented startup of the existing server binary.', flush=True)


def launch_case(label, directory, command, server, env, password, saved=None, build_command=None):
    free_port()
    process = None
    event_stream = None
    log_path = server.parent / (label + '.log')
    try:
        with log_path.open('wb') as log:
            began = time.monotonic()
            if build_command:
                subprocess.run(build_command, cwd=server, env=env, stdout=log,
                               stderr=subprocess.STDOUT, check=True, timeout=300)
            process = start(command, directory, env, log)
            plain = urllib.request.build_opener(urllib.request.ProxyHandler({}))
            deadline = began + 300
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(f'{label} exited before startup')
                # Wait for our own process to bind successfully before making
                # any requests, including when another process races for 8080.
                if b'PicoBrew Hub ready' not in log_path.read_bytes():
                    time.sleep(.1)
                    continue
                try:
                    with plain.open(BASE + '/healthz', timeout=.3) as response:
                        if response.status == 200:
                            break
                except (urllib.error.URLError, TimeoutError, ConnectionError):
                    time.sleep(.1)
            else:
                raise TimeoutError('Build/start exceeded five minutes')
            build_seconds = round(time.monotonic() - began, 3)
            with plain.open(BASE + '/', timeout=3) as response:
                page = response.read()
                assert response.status == 200 and b'<div id="root"></div>' in page
            asset = re.search(rb'<script\b[^>]*\bsrc="(/assets/[^"?]+\.js)"', page)
            assert asset, 'Embedded website is missing its JavaScript entry'
            with plain.open(BASE + asset.group(1).decode('ascii'), timeout=3) as response:
                assert response.status == 200 and len(response.read()) > 100, 'Embedded JavaScript is unavailable'
            jar = http.cookiejar.CookieJar()
            owner = urllib.request.build_opener(urllib.request.ProxyHandler({}), urllib.request.HTTPCookieProcessor(jar))
            assert api(owner, '/api/v1/auth/login', {'password': password})[0] == 200
            _, session = api(owner, '/api/v1/auth/session')
            assert api(owner, '/api/v1/devices')[1] == []
            if saved is None:
                status, saved = api(owner, '/api/v1/recipes', {
                    'name': 'Isolated shutdown smoke recipe', 'batch_liters': 5, 'efficiency': 55,
                    'type': 'All Grain', 'boil_liters': 6.5, 'boil_minutes': 60,
                    'notes': 'Must survive Ctrl-C and a restart from another directory.',
                    'ingredients': [{'name': 'Pale malt', 'kind': 'fermentable', 'type': 'Grain',
                                     'amount': 1.2, 'unit': 'kg', 'use': 'mash'}],
                    'steps': [{'name': 'Mash', 'kind': 'mash', 'target_c': 67, 'minutes': 60, 'location': 1},
                              {'name': 'Boil', 'kind': 'boil', 'target_c': 100, 'minutes': 60, 'location': 2}],
                }, session['csrf_token'])
                assert status == 201 and saved['revision'] == 1
                persistence = 'created'
            else:
                assert api(owner, '/api/v1/recipes/' + saved['id'])[1] == saved
                persistence = 'reopened_identical'
            event_stream = stream(jar)
            _, _, _, ended, state = event_stream
            stop = time.monotonic()
            terminal_interrupt(process)
            process.wait(timeout=3)
            assert ended.wait(max(.05, 3 - (time.monotonic() - stop))), 'SSE did not close promptly'
            assert state.get('clean_eof'), state
            assert listener_closed(), 'An orphan server is still listening'
            if not WINDOWS:
                try:
                    os.killpg(process.pid, 0)
                except ProcessLookupError:
                    pass
                else:
                    raise AssertionError('An orphan launcher remains in its process group')
            stop_seconds = round(time.monotonic() - stop, 3)
            assert stop_seconds < 3, stop_seconds
            expected_exit = process.returncode in (0, 130, -signal.SIGINT, 2)
            if WINDOWS:
                # GNU Make may exit with STATUS_CONTROL_C_EXIT when the console
                # group receives CTRL_BREAK. Only accept that exact status after
                # the SSE/port/shutdown checks above have proved graceful cleanup.
                expected_exit |= process.returncode & 0xffffffff == 0xc000013a
            assert expected_exit, process.returncode
            print(json.dumps({'entry_directory': label, 'build_and_start_seconds': build_seconds,
                              'shutdown_seconds': stop_seconds, 'exit_code': process.returncode,
                              'sse_clean_eof': True, 'listener_closed': True,
                              'recipe_persistence': persistence}), flush=True)
            return saved
    except Exception:
        if log_path.exists():
            print(log_path.read_text(encoding='utf-8', errors='replace')[-18000:].replace(password, '[redacted]'), flush=True)
        raise
    finally:
        if event_stream:
            connection, response, reader, _, _ = event_stream
            response.close()
            connection.close()
            reader.join(timeout=1)
        cleanup(process)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--make', action='store_true', help='Use available make server-start entry points')
    args = parser.parse_args()
    selected_go = shutil.which(os.environ.get('GO', 'go'))
    if not selected_go:
        raise SystemExit('Go is required for this test.')
    make = shutil.which(os.environ.get('MAKE', 'make')) if args.make else None
    if args.make and not make:
        raise SystemExit('--make requires GNU Make.')
    ensure_windows_console()
    password = secrets.token_urlsafe(30)
    env = os.environ | {'HUB_PASSWORD': password}
    # This fixture intentionally excludes Git metadata, databases and caches.
    env['GOFLAGS'] = (env.get('GOFLAGS', '') + ' -buildvcs=false').strip()
    with tempfile.TemporaryDirectory(prefix='picobrew server start ') as temporary:
        checkout = Path(temporary)
        env, go = go_only_environment(checkout, env, selected_go)
        server = copy_sources(checkout)
        if make:
            command = [make, 'server-start']
            if (checkout / 'Makefile').is_file():
                saved = launch_case('repository-root', checkout, command, server, env, password)
                launch_case('server-directory', server, command, server, env, password, saved)
            else:
                saved = launch_case('server-directory', server, command, server, env, password)
                launch_case('server-directory-restart', server, command, server, env, password, saved)
            failed_build_does_not_start(server, env, make)
            standalone = copy_sources(checkout / 'standalone copy', include_root=False)
            assert not (standalone.parent / 'Makefile').exists()
            saved = launch_case('standalone-server', standalone, command, standalone, env, password)
            launch_case('standalone-server-restart', standalone, command, standalone, env, password, saved)
        else:
            binary = server / ('picobrew-hub.exe' if WINDOWS else 'picobrew-hub')
            build = [go, 'build', '-trimpath', '-o', str(binary), './cmd/picobrew-hub']
            command = [str(binary), '--data', './data']
            saved = launch_case('direct-binary', server, command, server, env, password, build_command=build)
            launch_case('direct-binary-restart', server, command, server, env, password, saved)
    print('Go-only server startup, embedded website, terminal interruption and persistence checks passed.', flush=True)


if __name__ == '__main__':
    main()
