// @vitest-environment jsdom
import { createElement } from 'react'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { cleanup, render, screen, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { HubContext, type HubState } from '../context'
import { setCSRF } from '../lib/api'
import Explorer from './Explorer'

const fetchMock = vi.fn<typeof fetch>()
const originalShowModal = Object.getOwnPropertyDescriptor(HTMLDialogElement.prototype, 'showModal')
beforeEach(() => {
  vi.stubGlobal('fetch', fetchMock)
  setCSRF('explorer-upload')
  Object.defineProperty(HTMLDialogElement.prototype, 'showModal', {
    configurable: true,
    value(this: HTMLDialogElement) {
      this.open = true
    },
  })
})
afterEach(() => {
  cleanup()
  vi.restoreAllMocks()
  vi.unstubAllGlobals()
  if (originalShowModal)
    Object.defineProperty(HTMLDialogElement.prototype, 'showModal', originalShowModal)
  else Reflect.deleteProperty(HTMLDialogElement.prototype, 'showModal')
})

it('uses a file picker and multipart transport for upload operations while preserving JSON operations', async () => {
  fetchMock.mockImplementation(
    async (url) =>
      new Response(
        JSON.stringify(
          String(url).endsWith('/openapi.json')
            ? {
                paths: {
                  '/firmware/packages': {
                    post: {
                      summary: 'Upload firmware package',
                      requestBody: {
                        content: {
                          'multipart/form-data': {
                            schema: {
                              type: 'object',
                              properties: { file: { type: 'string', format: 'binary' } },
                            },
                          },
                        },
                      },
                    },
                  },
                  '/firmware/settings': {
                    put: {
                      summary: 'Configure firmware source',
                      requestBody: {
                        content: {
                          'application/json': { example: { repository: 'owner/repo', ref: '' } },
                        },
                      },
                    },
                  },
                },
              }
            : { id: 'stored-package' }
        ),
        { headers: { 'Content-Type': 'application/json' } }
      )
  )
  const user = userEvent.setup()
  const hub = {
    devices: [],
    recipes: [],
    brews: [],
    refresh: vi.fn(async () => {}),
    notify: vi.fn(),
  } as unknown as HubState
  render(createElement(HubContext.Provider, { value: hub }, createElement(Explorer)))
  const picker = await screen.findByLabelText('File to upload')
  expect(screen.queryByLabelText('JSON request body')).toBeNull()
  await user.click(screen.getByLabelText('Allow requests that change data or control a machine'))
  expect((screen.getByRole('button', { name: 'Send request' }) as HTMLButtonElement).disabled).toBe(
    true
  )
  const file = new File(['firmware fixture'], 'firmware.zip', { type: 'application/zip' })
  await user.upload(picker, file)
  await user.click(screen.getByRole('button', { name: 'Send request' }))
  await user.click(within(screen.getByRole('dialog')).getByRole('button', { name: 'Send request' }))
  await screen.findByText(/stored-package/)
  const request = fetchMock.mock.calls.find(([, options]) => options?.method === 'POST')![1]
  expect((request?.body as FormData).get('file')).toBe(file)
  expect(request?.headers).toEqual({ Accept: '*/*', 'X-CSRF-Token': 'explorer-upload' })
  await user.click(screen.getByRole('button', { name: /PUT.*\/firmware\/settings/ }))
  expect(screen.queryByLabelText('File to upload')).toBeNull()
  expect((screen.getByLabelText('JSON request body') as HTMLTextAreaElement).value).toContain(
    'owner/repo'
  )
})
