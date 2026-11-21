import sys
import unittest
from pathlib import Path
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import store_play
import store_apple


class TesterDistributionTest(unittest.TestCase):
    def test_play_picks_only_the_internal_track_returned_by_google(self):
        self.assertEqual("internal", store_play.internal_track([{"track": "production"}, {"track": "internal"}]))
        self.assertEqual("qa", store_play.internal_track([{"track": "qa"}]))
        with self.assertRaisesRegex(ValueError, "two possible"):
            store_play.internal_track([{"track": "qa"}, {"track": "internal"}])
        with self.assertRaisesRegex(ValueError, "internal"):
            store_play.internal_track([{"track": "production"}])

    def test_external_testflight_groups_are_never_offered(self):
        api = Mock(return_value={"data": [
            {"id": "a", "attributes": {"name": "Staff", "isInternalGroup": True}},
            {"id": "b", "attributes": {"name": "Public", "isInternalGroup": False}},
        ]})
        self.assertEqual([{"id": "a", "name": "Staff"}], store_apple.groups(api, "app"))

    def test_archive_identity_must_match_reviewed_build(self):
        expected = {"identifier": "com.example.game", "version": "1.0", "build": "24"}
        actual = {"CFBundleIdentifier": "com.example.game", "CFBundleShortVersionString": "1.0", "CFBundleVersion": "23"}
        with self.assertRaisesRegex(ValueError, "build"):
            store_apple.check_archive(actual, expected)

    def test_play_uploads_the_bundle_updates_only_internal_and_commits_last(self):
        from tempfile import TemporaryDirectory
        with TemporaryDirectory() as temp:
            artifact = Path(temp) / "Game.aab"
            artifact.write_bytes(b"signed fixture")
            api = Mock(side_effect=[{"id": "edit"}, {"tracks": [{"track": "production"}, {"track": "internal"}]},
                                    {}, {"versionCode": 24}, {}, {}])
            result = store_play.deliver("fixture token", {"identifier": "org.example.game", "build": "24"}, artifact, api)
            calls = [c.args for c in api.call_args_list]
            self.assertEqual(artifact, calls[3][2])
            self.assertTrue(calls[4][1].endswith("/tracks/internal"))
            self.assertEqual([{"versionCodes": ["24"], "status": "completed"}], calls[4][2]["releases"])
            self.assertTrue(calls[5][1].endswith(":commit"))
            self.assertIn("accepted", result)

    def test_failed_or_conflicting_play_release_discards_the_edit(self):
        scenarios = [
            ({"track": "internal"}, {"versionCode": 23}, "version code"),
            ({"track": "internal", "releases": [{"status": "draft"}]}, {"versionCode": 24}, "unfinished"),
            ({"track": "qa", "releases": [{"status": "completed", "versionCodes": ["25"]}]},
             {"versionCode": 24}, "newer"),
        ]
        for track, uploaded, message in scenarios:
            api = Mock(side_effect=[{"id": "e"}, {"tracks": [track]}, {}, uploaded, {}])
            with self.subTest(message), self.assertRaisesRegex(ValueError, message):
                store_play.deliver("token", {"identifier": "org.example.game", "build": "24"}, Path("fixture.aab"), api)
            self.assertEqual("DELETE", api.call_args.args[0])
            self.assertFalse(any(c.args[0] == "PUT" for c in api.call_args_list))

    def test_testflight_resume_waits_for_the_reviewed_build_and_reports_actual_beta_state(self):
        from unittest.mock import patch
        build = {"id": "reviewed", "attributes": {"processingState": "VALID"}}
        api = Mock(side_effect=[
            {"data": [build]}, {"data": {"attributes": {"name": "Staff", "isInternalGroup": True}}},
            {"data": []}, {}, {"data": {"attributes": {"internalBuildState": "MISSING_EXPORT_COMPLIANCE"}}},
        ])
        with patch.object(store_apple.time, "sleep"):
            result = store_apple.finish(api, "app", {"version": "2.0", "build": "24"}, "staff", None)
        self.assertIn("missing export compliance", result)
        self.assertIn("filter%5Bversion%5D=24", api.call_args_list[0].args[1])
        self.assertEqual(("POST", "/betaGroups/staff/relationships/builds", {"data": [{"type": "builds", "id": "reviewed"}]}),
                         api.call_args_list[3].args)

    def test_failed_and_expired_testflight_builds_cannot_be_assigned(self):
        for attributes in [{"processingState": "FAILED"}, {"processingState": "VALID", "expired": True}]:
            api = Mock()
            with self.assertRaises(ValueError):
                store_apple.finish(api, "app", {}, "staff", {"attributes": attributes})
            api.assert_not_called()

    def test_xcode_verifies_archive_before_an_internal_only_upload(self):
        import plistlib
        from tempfile import TemporaryDirectory
        from unittest.mock import patch
        with TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "Game.xcarchive"
            archive.mkdir()
            (archive / "Info.plist").write_bytes(plistlib.dumps({"ApplicationProperties": {
                "CFBundleIdentifier": "org.example.game", "CFBundleShortVersionString": "2.0", "CFBundleVersion": "24",
            }}))
            account = {"file": "/fixture/AuthKey.p8", "issuer": "fixture", "keyId": "EXAMPLE123"}
            spec = {"identifier": "org.example.game", "version": "2.0", "build": "24", "team": "TEAM"}
            with patch.object(store_apple.subprocess, "run") as run:
                store_apple.upload(root / "Demo.xcodeproj", root, account, spec)
                self.assertIn("archive", run.call_args_list[0].args[0])
                self.assertIn("-exportArchive", run.call_args_list[1].args[0])
                options = plistlib.loads((root / "ExportOptions.plist").read_bytes())
                self.assertIs(options["testFlightInternalTestingOnly"], True)
                self.assertIs(options["manageAppVersionAndBuildNumber"], False)
                run.reset_mock()
                with self.assertRaisesRegex(ValueError, "identifier"):
                    store_apple.upload(root / "Demo.xcodeproj", root, account, dict(spec, identifier="wrong.app"))
                self.assertEqual(1, run.call_count)

    def test_google_reuses_refresh_grant_without_browser_and_uses_fixed_token_endpoint(self):
        import json
        from tempfile import TemporaryDirectory
        from unittest.mock import patch
        import store_auth
        with TemporaryDirectory() as temp:
            key = Path(temp) / "client.json"
            key.write_text(json.dumps({"installed": {"client_id": "fixture", "token_uri": "https://invalid.test"}}))
            with patch.object(store_auth, "request", return_value={"access_token": "access"}) as api:
                self.assertEqual("access", store_auth.google_token({"file": str(key), "refresh": "refresh"}))
                self.assertEqual("https://oauth2.googleapis.com/token", api.call_args.args[1])
                self.assertIn(b"grant_type=refresh_token", api.call_args.args[2])
            with self.assertRaisesRegex(ValueError, "Connect"):
                store_auth.google_token({"file": str(key)})

    def test_connection_refuses_credentials_from_the_export_tree_before_contacting_either_store(self):
        import tester_distribution
        from tempfile import TemporaryDirectory
        with TemporaryDirectory() as temp:
            key = Path(temp) / "secret.p8"
            key.touch()
            with self.assertRaisesRegex(ValueError, "outside"):
                tester_distribution.run("connect", {"root": temp, "spec": {"platform": "iOS"}, "account": {"file": str(key)}})

    def test_testflight_already_assigned_on_a_later_page(self):
        build = {"id": "reviewed", "attributes": {"processingState": "VALID"}}
        api = Mock(side_effect=[
            {"data": {"attributes": {"name": "Staff", "isInternalGroup": True}}},
            {"data": [], "links": {"next": store_apple.API + "/nextPage"}},
            {"data": [{"id": "reviewed"}]}, {"data": {"attributes": {"internalBuildState": "IN_BETA_TESTING"}}},
        ])
        receipt = store_apple.finish(api, "app", {"version": "2", "build": "4"}, "staff", build)
        self.assertIn("in beta testing", receipt)
        self.assertTrue(all(c.args[0] == "GET" for c in api.call_args_list))

    def test_play_checks_for_an_unfinished_release_before_upload(self):
        api = Mock(side_effect=[{"id": "e"}, {"tracks": [{"track": "internal", "releases": [{"status": "draft"}]}]}, {}])
        with self.assertRaisesRegex(ValueError, "unfinished"):
            store_play.deliver("token", {"identifier": "org.example.game", "build": "24"}, Path("demo.aab"), api)
        self.assertEqual(["POST", "GET", "DELETE"], [c.args[0] for c in api.call_args_list])

    def test_google_browser_consent_returns_bound_pkce_grant(self):
        import hashlib
        from threading import Thread
        from unittest.mock import patch
        from urllib.error import HTTPError
        from urllib.parse import parse_qs, urlencode, urlsplit
        from urllib.request import urlopen
        import store_auth
        query = {}
        outcomes = []
        def launch(url):
            query.update(parse_qs(urlsplit(url).query))
            self.assertEqual("accounts.google.com", urlsplit(url).hostname)
            def browser():
                for state in ["wrong", query["state"][0]]:
                    callback = query["redirect_uri"][0] + "?" + urlencode({"state": state, "code": "granted"})
                    try:
                        with urlopen(callback, timeout=5) as response:
                            outcomes.append(response.status)
                    except HTTPError as error:
                        outcomes.append(error.code)
            nonlocal thread
            thread = Thread(target=browser)
            thread.start()
            return True
        thread = None
        with patch.object(store_auth.webbrowser, "open", side_effect=launch):
            grant = store_auth.google_consent({"client_id": "studio"})
        thread.join(5)
        self.assertEqual([400, 200], outcomes)
        self.assertEqual("granted", grant["code"])
        self.assertEqual(query["code_challenge"][0], store_auth.b64(hashlib.sha256(grant["code_verifier"].encode()).digest()))

    def test_deliver_accepts_resolved_artifact_inside_symlinked_temp_dir(self):
        import tester_distribution
        from tempfile import TemporaryDirectory
        from unittest.mock import patch
        with TemporaryDirectory() as base:
            real_root = Path(base).resolve()
            game = real_root / "game"
            game.mkdir()
            key = real_root / "client.json"
            key.write_text('{"installed":{"client_id":"cid"}}')
            real_tmp = real_root / "real_tmp"
            real_tmp.mkdir()
            link_tmp = real_root / "link_tmp"
            link_tmp.symlink_to(real_tmp)

            class FakeTempDir:
                def __init__(self, **_):
                    self.name = str(link_tmp)
                def __enter__(self):
                    return self.name
                def __exit__(self, *_):
                    return False

            def fake_export(_root, folder, *_args):
                out = Path(folder) / "Game.aab"
                out.write_bytes(b"aab")
                return [str(out.resolve())]

            data = {
                "root": str(game),
                "spec": {"platform": "Android", "identifier": "org.example.game", "version": "1.0", "build": "2"},
                "account": {"file": str(key), "refresh": "tok"},
                "target": "internal",
                "label": "dev@example.com",
                "job": {"platform": "Android", "store": True},
                "godot": "godot",
                "runtime": str(real_root),
                "signing": {},
            }
            with patch.object(tester_distribution, "google_token", return_value="access"), \
                    patch.object(tester_distribution, "request", return_value={"email": "dev@example.com"}), \
                    patch.object(tester_distribution.tempfile, "TemporaryDirectory", FakeTempDir), \
                    patch.object(tester_distribution.game_export, "export", side_effect=fake_export), \
                    patch.object(tester_distribution.store_play, "deliver", return_value="ok") as deliver:
                self.assertEqual("ok", tester_distribution.run("deliver", data))
                deliver.assert_called_once()

    def test_apple_token_validates_der_signature_bounds(self):
        import subprocess
        from unittest.mock import patch
        import store_auth
        account = {"keyId": "ABCDE12345", "issuer": "12345678-1234-1234-1234-1234567890ab", "file": "/tmp/key.p8"}
        r_bytes = b"\x00" + b"\x01" * 32
        s_bytes = b"\x02" * 32
        der_body = bytes([2, len(r_bytes)]) + r_bytes + bytes([2, len(s_bytes)]) + s_bytes
        valid_der = bytes([0x30, len(der_body)]) + der_body
        with patch.object(store_auth.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, valid_der, b"")):
            token = store_auth.apple_token(account)
            self.assertEqual(3, len(token.split(".")))
        for bad in (
            b"\x30\x06\x02\x20\x01\x02\x03\x04",
            b"\x30\x46\x02\x22" + b"\x01" * 34 + b"\x02\x20" + b"\x02" * 32,
            b"\x30\x81\x44\x02\x20" + b"\x01" * 32 + b"\x02\x20" + b"\x02" * 32,
        ):
            with self.subTest(bad=bad), \
                    patch.object(store_auth.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, bad, b"")):
                with self.assertRaisesRegex(ValueError, "Invalid Apple signing key"):
                    store_auth.apple_token(account)

    def test_xcodebuild_failure_does_not_leak_auth_key_details(self):
        import subprocess
        from tempfile import TemporaryDirectory
        from unittest.mock import patch
        with TemporaryDirectory() as temp:
            root = Path(temp)
            account = {"file": "/secret/path/AuthKey_SECRET.p8", "issuer": "secret-issuer-uuid", "keyId": "SECRETKEY1"}
            spec = {"identifier": "org.example.game", "version": "1.0", "build": "1", "team": "TEAM123456"}
            err = subprocess.CalledProcessError(65, ["/usr/bin/xcodebuild", "-authenticationKeyPath", account["file"]])
            with patch.object(store_apple.subprocess, "run", side_effect=err):
                with self.assertRaises(ValueError) as ctx:
                    store_apple.upload(root / "Game.xcodeproj", root, account, spec)
            self.assertNotIn("AuthKey_SECRET", str(ctx.exception))
            self.assertIn("65", str(ctx.exception))

    def test_store_http_rejects_oversized_response_and_unexpected_host(self):
        from unittest.mock import MagicMock, patch
        import store_http
        with self.assertRaisesRegex(ValueError, "Unexpected store endpoint"):
            store_http.request("GET", "https://example.com/v1/apps")
        fake_response = MagicMock(status=200, reason="OK")
        fake_response.read.return_value = b"x" * (2_000_000 + 1)
        fake_conn = MagicMock()
        fake_conn.getresponse.return_value = fake_response
        with patch.object(store_http.http.client, "HTTPSConnection", return_value=fake_conn):
            with self.assertRaisesRegex(store_http.StoreError, "2 MB"):
                store_http.request("GET", "https://api.appstoreconnect.apple.com/v1/apps")
