"""Config format (shared with the runtime) and parameter validation, Python side."""

import unittest

from support import ltm


class ParseConfigTests(unittest.TestCase):
    def test_default_template_parses_to_defaults_without_warnings(self):
        settings, warnings = ltm.parse_config(ltm.default_config_text())
        self.assertEqual(settings, ltm.default_settings())
        self.assertEqual(warnings, [])

    def test_template_documents_every_key(self):
        text = ltm.default_config_text()
        for key in ("enabled", "scale", "opacity") + ltm.FEATURE_KEYS:
            self.assertRegex(text, r"(?m)^{} = ".format(key))
        self.assertIn("1.1 to 2.0", text)
        self.assertIn("0.0 (transparent) to 1.0 (opaque)", text)

    def test_empty_config_uses_defaults_and_warns_about_missing_values(self):
        settings, warnings = ltm.parse_config("")
        self.assertEqual(settings, ltm.default_settings())
        self.assertTrue(any("scale is missing" in w for w in warnings))
        self.assertTrue(any("opacity is missing" in w for w in warnings))

    def test_valid_values(self):
        text = (
            "﻿; comment\r\n"
            "[General]\r\n"
            "ENABLED = off\r\n"
            "Scale = 1.3   ; inline comment\r\n"
            "opacity=0,5 # decimal comma, hash comment\r\n"
            "[ features ]\r\n"
            "text_log = no\r\n"
            "evidence_menu = 0\r\n"
            "dialogue_box = YES\r\n"
        )
        settings, warnings = ltm.parse_config(text)
        self.assertEqual(warnings, [])
        self.assertFalse(settings["enabled"])
        self.assertEqual(settings["scale"], 1.3)
        self.assertEqual(settings["opacity"], 0.5)
        self.assertFalse(settings["features"]["text_log"])
        self.assertFalse(settings["features"]["evidence_menu"])
        self.assertTrue(settings["features"]["dialogue_box"])
        self.assertTrue(settings["features"]["pause_when_unfocused"])
        self.assertTrue(settings["features"]["save_menu"])
        self.assertTrue(settings["features"]["gamepad_navigation"])
        self.assertTrue(settings["features"]["selection_border"])

    def test_boundaries_are_inclusive(self):
        settings, warnings = ltm.parse_config("[general]\nscale = 2.0\nopacity = 0\n")
        self.assertEqual((settings["scale"], settings["opacity"]), (2.0, 0.0))
        self.assertEqual(warnings, [])
        settings, warnings = ltm.parse_config("[general]\nscale = 1.1\nopacity = 1\n")
        self.assertEqual((settings["scale"], settings["opacity"]), (1.1, 1.0))

    def test_out_of_range_values_fall_back_with_warning(self):
        settings, warnings = ltm.parse_config("[general]\nscale = 2.5\nopacity = -0.1\n")
        self.assertEqual(settings["scale"], ltm.SCALE_DEFAULT)
        self.assertEqual(settings["opacity"], ltm.OPACITY_DEFAULT)
        self.assertEqual(len([w for w in warnings if "outside" in w]), 2)

    def test_malformed_values_fall_back_with_warning(self):
        text = ("[general]\nenabled = maybe\nscale = big\nopacity = nan\n"
                "[features]\ntext_log = sometimes\n")
        settings, warnings = ltm.parse_config(text)
        self.assertEqual(settings, ltm.default_settings())
        self.assertEqual(len(warnings), 4)

    def test_malformed_lines_are_ignored_with_warning(self):
        settings, warnings = ltm.parse_config("[general\nscale = 1.2\njust text\n[general]\nopacity = 0.5\n")
        # 'scale' was in a broken section, so it does not count.
        self.assertEqual(settings["scale"], ltm.SCALE_DEFAULT)
        self.assertEqual(settings["opacity"], 0.5)
        self.assertTrue(any("malformed section header" in w for w in warnings))
        self.assertTrue(any("expected 'key = value'" in w for w in warnings))

    def test_duplicate_key_last_wins_with_warning(self):
        settings, warnings = ltm.parse_config("[general]\nscale = 1.2\nscale = 1.4\nopacity = 1\n")
        self.assertEqual(settings["scale"], 1.4)
        self.assertTrue(any("more than once" in w for w in warnings))

    def test_unknown_keys_and_sections_are_ignored(self):
        settings, warnings = ltm.parse_config(
            "[general]\nscale = 1.2\nopacity = 1\nfuture = 3\n[features]\nnew_feature = false\n[other]\nx = y\n")
        self.assertEqual(warnings, [])
        self.assertNotIn("new_feature", settings["features"])

    def test_value_with_hash_inside_is_not_a_comment(self):
        # Like the runtime, ';' or '#' only starts a comment at the start or after whitespace.
        settings, warnings = ltm.parse_config("[general]\nscale = 1.2#x\nopacity = 1\n")
        self.assertEqual(settings["scale"], ltm.SCALE_DEFAULT)
        self.assertTrue(any("not a number" in w for w in warnings))


class UpdateConfigTextTests(unittest.TestCase):
    def test_none_creates_template_with_given_values(self):
        text = ltm.update_config_text(None, 1.3, None)
        settings, warnings = ltm.parse_config(text)
        self.assertEqual(warnings, [])
        self.assertEqual(settings["scale"], 1.3)
        self.assertEqual(settings["opacity"], ltm.OPACITY_DEFAULT)

    def test_no_values_returns_text_unchanged(self):
        text = "[general]\nscale = 1.7 ; mine\nweird stuff\n"
        self.assertEqual(ltm.update_config_text(text), text)

    def test_only_given_values_change_and_everything_else_is_kept(self):
        text = ("﻿; my notes\r\n[general]\r\nenabled = false\r\nscale = 1.7    ; mine\r\n"
                "opacity = 0.3\r\nmy_key = keep me\r\n\r\n[features]\r\ntext_log = false\r\n"
                "unknown_feature = yes\r\n[custom]\r\nscale = 9\r\n")
        result = ltm.update_config_text(text, scale=1.2)
        expected = text.replace("scale = 1.7    ; mine", "scale = 1.2    ; mine")
        self.assertEqual(result, expected)
        settings, _ = ltm.parse_config(result)
        self.assertEqual(settings["scale"], 1.2)
        self.assertEqual(settings["opacity"], 0.3)
        self.assertFalse(settings["enabled"])
        self.assertFalse(settings["features"]["text_log"])

    def test_missing_keys_are_inserted_into_general(self):
        text = "[general]\nenabled = true\n\n[features]\ntext_log = false\n"
        result = ltm.update_config_text(text, 1.4, 0.5)
        self.assertEqual(result, "[general]\nenabled = true\nscale = 1.4\nopacity = 0.5\n\n"
                                 "[features]\ntext_log = false\n")

    def test_missing_general_section_is_added(self):
        text = "[features]\ntext_log = false"
        result = ltm.update_config_text(text, 1.4, None)
        settings, _ = ltm.parse_config(result)
        self.assertEqual(settings["scale"], 1.4)
        self.assertFalse(settings["features"]["text_log"])
        self.assertTrue(result.startswith(text + "\n"))

    def test_idempotent(self):
        once = ltm.update_config_text(ltm.default_config_text(), 1.3, 0.8)
        self.assertEqual(ltm.update_config_text(once, 1.3, 0.8), once)


class ValidateSettingsTests(unittest.TestCase):
    def test_valid(self):
        self.assertEqual(ltm.validate_settings("1.1", "0"), (1.1, 0.0))
        self.assertEqual(ltm.validate_settings("2", "1.0"), (2.0, 1.0))
        self.assertEqual(ltm.validate_settings("1,3", None), (1.3, None))
        self.assertEqual(ltm.validate_settings(None, None), (None, None))

    def test_invalid(self):
        for scale in ("1.09", "2.01", "abc", "nan", "inf", "", "1_5", "0x2", "1e400"):
            with self.subTest(scale=scale):
                with self.assertRaises(ltm.ModError):
                    ltm.validate_settings(scale, None)
        for opacity in ("-0.1", "1.01", "NaN", "-inf", "x"):
            with self.subTest(opacity=opacity):
                with self.assertRaises(ltm.ModError):
                    ltm.validate_settings(None, opacity)


if __name__ == "__main__":
    unittest.main()
