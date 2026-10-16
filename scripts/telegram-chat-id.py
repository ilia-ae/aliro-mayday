#!/usr/bin/env python3
"""Find Telegram chat IDs without sending messages or saving the bot token.

Copyright (c) 2026 Mayday / Ilia Arestov. See ../LICENSE.
Uses only Python's standard library. Run from a local terminal, after sending
/start to your own bot. No token arguments, files or environment variables.
"""
import argparse
import getpass
import json
import re
import ssl
import sys
import warnings
from urllib.error import HTTPError, URLError
from urllib.request import HTTPRedirectHandler, HTTPSHandler, Request, build_opener


class DiscoveryError(Exception):
    """A message safe to display: never include an exception's URL or body."""


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def discover(token, opener=None):
    if not re.fullmatch(r"[0-9]{1,16}:[A-Za-z0-9_-]{20,110}", token):
        raise DiscoveryError("Invalid token format. Copy the token from @BotFather.")
    if opener is None:
        opener = build_opener(HTTPSHandler(context=ssl.create_default_context()), NoRedirect())
    request = Request(
        f"https://api.telegram.org/bot{token}/getUpdates",
        data=json.dumps({"limit": 100, "timeout": 0}).encode(),
        headers={"Content-Type": "application/json"}, method="POST",
    )
    try:
        with opener.open(request, timeout=10) as response:
            raw = response.read(262145)
        if len(raw) > 262144:
            raise DiscoveryError("Response too large. Use a dedicated notification bot.")
        result = json.loads(raw)
    except HTTPError as error:
        messages = {
            401: "Token rejected. Check @BotFather; do not paste the token into an issue.",
            409: "This bot has a webhook or another poller. Use a new dedicated bot; no settings were changed.",
            429: "Telegram rate limit. Wait and try again later.",
        }
        raise DiscoveryError(messages.get(error.code, f"Telegram HTTP error {error.code}. No messages were sent.")) from None
    except (URLError, TimeoutError, OSError):
        raise DiscoveryError("HTTPS connection failed. Check Internet access, time and trusted certificates.") from None
    except (ValueError, UnicodeError):
        raise DiscoveryError("Invalid Telegram response. No messages were sent.") from None
    if not isinstance(result, dict) or result.get("ok") is not True or not isinstance(result.get("result"), list):
        raise DiscoveryError("Telegram did not return an update list. No messages were sent.")
    chats = set()
    for update in result["result"]:
        message = update.get("message", {}) if isinstance(update, dict) else {}
        chat = message.get("chat", {}) if isinstance(message, dict) else {}
        if isinstance(chat, dict) and type(chat.get("id")) is int and chat.get("type") in {"private", "group", "supergroup"}:
            chats.add((chat["id"], chat["type"]))
    return sorted(chats)


def main():
    parser = argparse.ArgumentParser(description="Read chat IDs from your own bot. Sends no messages; saves no token.")
    parser.parse_args()
    if not sys.stdin.isatty():
        print("Run in a local interactive terminal. The token prompt must not echo.", file=sys.stderr)
        return 2
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("error", getpass.GetPassWarning)
            token = getpass.getpass("Bot token (hidden; not saved): ")
        chats = discover(token)
        token = None
        if not chats:
            print("No chats found. Send /start to your bot, then run this tool again.")
            print("For a group, add the bot and send /start@your_bot_username in that group.")
            return 1
        print("Copy the intended chat ID, including its minus sign, into the controller panel:")
        for chat_id, kind in chats:
            print(f"{chat_id}\t{kind}")
        print("Only IDs/types displayed; no message text or user names. No messages sent.")
        return 0
    except DiscoveryError as error:
        print(str(error), file=sys.stderr)
        return 1
    except getpass.GetPassWarning:
        print("Hidden input is unavailable. Use a local terminal; token was not read.", file=sys.stderr)
        return 2
    except (KeyboardInterrupt, EOFError):
        print("Cancelled; token not saved.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
