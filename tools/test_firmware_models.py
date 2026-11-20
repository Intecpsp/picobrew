"""Check product/family routing before any hardware image is built."""

import contextlib
import io
import unittest
from unittest.mock import patch

import firmware_models as models


class FirmwareModelRoutingTests(unittest.TestCase):
    def test_s_and_pro_resolve_to_shared_source_but_distinct_build_models(self):
        for model in ("pico-s", "pico-pro"):
            with self.subTest(model=model):
                selected = models.resolve_model(model)
                self.assertEqual(selected["family"], "pico-s-pro")
                self.assertEqual(models.resolve_model(machine=model), selected)
                self.assertEqual(models.resolve_model(model, "pico-s-pro"), selected)
                command = models.make_command("build", selected, "make", "Python with spaces/python")
                self.assertIn(f"MODEL={model}", command)
                self.assertIn("PYTHON=Python with spaces/python", command)

    def test_legacy_family_target_keeps_the_documented_pro_default(self):
        self.assertEqual(models.resolve_model()["id"], "pico-pro")
        self.assertEqual(models.resolve_model(machine="pico-s-pro")["id"], "pico-pro")

    def test_package_routes_each_product_to_its_component_build_and_package_target(self):
        for model in ("pico-pro", "pico-s"):
            with self.subTest(model=model):
                command = models.make_command("package", models.resolve_model(model))
                self.assertIn("firmware-package", command)
                self.assertIn(f"MODEL={model}", command)

    def test_contradictory_product_and_family_are_rejected(self):
        for model, machine in (("pico-s", "pico-pro"), ("pico-pro", "pico-c"),
                               ("pico-s", "../outside")):
            with self.subTest(model=model, machine=machine), self.assertRaises(ValueError):
                models.resolve_model(model, machine)

    def test_planned_models_never_dispatch_a_relabelled_s_pro_image(self):
        for action in ("build", "package"):
            for model in ("zymatic",):
                with self.subTest(action=action, model=model), patch.object(models.subprocess, "run") as run, \
                        contextlib.redirect_stderr(io.StringIO()) as errors:
                    self.assertEqual(models.main([action, "--model", model]), 2)
                    self.assertIn("no implemented custom firmware", errors.getvalue())
                    run.assert_not_called()

    def test_reference_targets_use_their_own_source_and_never_infer_z_variant(self):
        for model in ('z-series','picostill','picoferm'):
            selected=models.resolve_model(model)
            self.assertIn(str(models.ROOT/'firmware'/model/'modified'),models.make_command('build',selected))
            with self.assertRaisesRegex(ValueError,'qualified USB'):
                models.make_command('usb-test',selected)
        self.assertEqual(models.resolve_model('z')['id'],'z-series')
        for model in ('z1','z2'):
            with self.assertRaises(ValueError):models.resolve_model(model)

    def test_c_uses_recovered_c_source_and_requires_explicit_display(self):
        selected=models.resolve_model('pico-c')
        self.assertEqual(selected['family'],'pico-c')
        for action in ('build','package'):
            with self.assertRaisesRegex(ValueError,'DISPLAY_VARIANT='):
                models.make_command(action,selected)
            for display in ('ssd1325','ssd1309'):
                command=models.make_command(action,selected,display=display)
                self.assertIn('MODEL=pico-c',command)
                self.assertIn('DISPLAY_VARIANT='+display,command)
                self.assertIn(str(models.ROOT/'firmware/pico-c/modified'),command)
        with self.assertRaises(ValueError):
            models.make_command('build',models.resolve_model('pico-s'),display='ssd1309')

    def test_unknown_ids_are_rejected_before_dispatch(self):
        with patch.object(models.subprocess, "run") as run, \
                contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(models.main(["build", "--model", "pico-unknown"]), 2)
            run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
