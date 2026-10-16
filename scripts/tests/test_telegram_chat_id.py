"""Offline regressions for token handling and read-only chat discovery."""
import importlib.util
import io
import json
from pathlib import Path
import unittest
from urllib.error import HTTPError, URLError

spec = importlib.util.spec_from_file_location("chat_id", Path(__file__).resolve().parents[1] / "telegram-chat-id.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
TOKEN = "12345:" + "X" * 35


class FakeOpener:
    def __init__(self, body=None, error=None):
        self.body, self.error, self.requests = body, error, []

    def open(self, request, timeout):
        self.requests.append(request)
        if self.error:
            raise self.error
        return io.BytesIO(self.body if isinstance(self.body, bytes) else json.dumps(self.body).encode())


class DiscoveryTests(unittest.TestCase):
    def test_reads_ids_once_without_acknowledging_updates_or_changing_filters(self):
        opener = FakeOpener({"ok": True, "result": [
            {"message": {"chat": {"id": 424242, "type": "private"}, "text": "private text"}},
            {"message": {"chat": {"id": -1001234567890, "type": "supergroup"}}},
            {"message": {"chat": {"id": 424242, "type": "private"}}},
            {"my_chat_member": {"chat": {"id": 99, "type": "private"}}},
        ]})
        self.assertEqual(module.discover(TOKEN, opener), [(-1001234567890, "supergroup"), (424242, "private")])
        self.assertEqual(len(opener.requests), 1)
        request = opener.requests[0]
        self.assertEqual(request.full_url, f"https://api.telegram.org/bot{TOKEN}/getUpdates")
        self.assertEqual(json.loads(request.data), {"limit": 100, "timeout": 0})

    def test_invalid_tokens_never_reach_network(self):
        for token in ["", "invalid", TOKEN + "/sendMessage", TOKEN + "\n", "../" + TOKEN]:
            opener = FakeOpener()
            with self.assertRaises(module.DiscoveryError):
                module.discover(token, opener)
            self.assertEqual(opener.requests, [])

    def test_errors_never_disclose_token_url_or_response_body(self):
        errors = [HTTPError(f"https://api.telegram.org/bot{TOKEN}/getUpdates", code, TOKEN, {}, None) for code in [401, 409, 429, 500]]
        errors.append(URLError(TOKEN))
        for error in errors:
            with self.assertRaises(module.DiscoveryError) as raised:
                module.discover(TOKEN, FakeOpener(error=error))
            self.assertNotIn(TOKEN, str(raised.exception))
            self.assertNotIn("https://", str(raised.exception))

    def test_malformed_or_oversized_responses_fail_closed(self):
        for body in [b"bad JSON", b"x" * 262145, [], {"ok": False, "description": TOKEN}, {"ok": True, "result": {}}]:
            with self.assertRaises(module.DiscoveryError) as raised:
                module.discover(TOKEN, FakeOpener(body))
            self.assertNotIn(TOKEN, str(raised.exception))

    def test_redirects_are_not_followed(self):
        self.assertIsNone(module.NoRedirect().redirect_request(None, None, 302, "Found", {}, "https://example.invalid"))


if __name__ == "__main__":
    unittest.main()
