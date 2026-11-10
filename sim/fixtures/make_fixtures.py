#!/usr/bin/env python3
"""Writes the simulator fixtures. Everything here is invented sample data."""
import json
import os
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

TZ = ZoneInfo("America/Los_Angeles")
NOW = datetime(2026, 11, 10, 14, 47, tzinfo=TZ)
HERE = os.path.dirname(__file__)


def t(**kw):
    return int((NOW + timedelta(**kw)).timestamp())


def at(day_offset, h, m=0):
    d = (NOW + timedelta(days=day_offset)).replace(hour=h, minute=m, second=0)
    return int(d.timestamp())


day = {
    "v": 1,
    "gen": t(minutes=-2),
    "hz": t(hours=12),
    "hl": "Two meetings left, one fire in prod, and the rest can wait.",
    "cards": [
        {"id": "cal:design-review", "k": "event", "p": 70, "at": t(minutes=12), "en": t(minutes=42),
         "t": "Design review: onboarding v2", "sh": "Design review", "s": "with Priya, Marco and Dee",
         "b": "Priya wants a yes or no on the new pricing step. Last week's notes flagged drop-off at step 3.",
         "m": "Google Meet  ·  30 min"},
        {"id": "slack:triage", "k": "chat", "p": 66, "t": "Slack, sorted",
         "r": [
             {"l": "Sam Ortiz", "d": "Wants your OK on the launch date by 4 PM", "g": "DM", "tr": 0, "u": True},
             {"l": "#incidents", "d": "Payments latency alert, you are on call", "g": "!", "tr": 0, "u": True},
             {"l": "#design", "d": "3 new replies on the onboarding thread", "g": "3", "tr": 1},
             {"l": "Rita Chen", "d": "Shared the Q4 deck for comments", "g": "DM", "tr": 1},
             {"l": "#general", "d": "Offsite RSVP closes Friday", "g": "1", "tr": 2},
         ]},
        {"id": "gh:queue", "k": "review", "p": 64, "t": "3 pull requests waiting on you", "sh": "3 PRs need you",
         "r": [
             {"l": "api #412", "d": "Rate limiter rewrite, review requested", "g": "2d", "tr": 0, "u": True},
             {"l": "c-ink #3", "d": "Your PR: Windows build failing", "g": "CI", "tr": 0, "u": True},
             {"l": "web #88", "d": "Checkout copy, approved by Sam", "g": "1d", "tr": 1},
         ]},
        {"id": "vercel:web", "k": "ship", "p": 62, "u": True, "t": "Production deploy failed", "sh": "web-app deploy failed",
         "s": "web-app  ·  main  ·  18 min ago",
         "b": "Missing env STRIPE_WEBHOOK_SECRET in checkout/route.ts. The last good deploy is still serving traffic."},
        {"id": "gmail:replies", "k": "mail", "p": 48, "t": "2 emails waiting on you", "sh": "2 emails to answer",
         "r": [
             {"l": "Alex, Vanta", "d": "Contract redlines, wants a call by Thursday", "g": "3d", "tr": 1},
             {"l": "Jordan Lee", "d": "Intro to the label partnerships team", "g": "1d", "tr": 2},
         ]},
        {"id": "focus:pm", "k": "focus", "p": 45, "st": t(minutes=43), "en": at(0, 16, 25),
         "t": "Free until 4:30. Finish the rate limiter review.", "sh": "Focus until 4:30",
         "b": "It is blocking Marco and Dee, and it is the oldest thing in your queue."},
        {"id": "linear:eng-231", "k": "task", "p": 44, "t": "ENG-231 is due tomorrow",
         "s": "Offline sync for mobile  ·  In Progress",
         "b": "Two sub-issues are still open. Marco is blocked on the schema decision."},
        {"id": "cal:1on1", "k": "event", "p": 50, "at": at(0, 16, 30), "en": at(0, 17, 0),
         "t": "1:1 with Dana", "s": "Weekly  ·  Room 4B"},
        {"id": "stripe:today", "k": "money", "p": 40, "t": "Revenue today", "big": "$4,820",
         "dl": "+18% vs a usual Tuesday", "s": "41 payments, 2 refunds",
         "sp": [8, 10, 9, 12, 14, 13, 18, 22, 21, 26, 30, 29, 34, 40, 38, 44, 52, 50, 57, 63, 61, 70, 76, 82]},
        {"id": "once:low-tide", "k": "music", "p": 35, "t": "“Low Tide” is climbing", "big": "12.4k",
         "dl": "+31% this week", "s": "Released 6 days ago. Spotify is 72% of plays.",
         "sp": [20, 22, 28, 35, 41, 52, 64]},
        {"id": "tavern:s14", "k": "quest", "p": 30, "at": at(4, 19, 0), "t": "The Sunken Archive",
         "s": "Session 14  ·  Saturday 7:00 PM",
         "b": "Your bard is at 34/41 HP with 2 spell slots left. The party left the archivist's key with the harbor guard."},
        {"id": "cal:standup", "k": "event", "p": 30, "at": at(1, 9, 30), "en": at(1, 9, 45), "t": "Standup",
         "s": "Daily  ·  Zoom"},
    ],
}

empty = {"v": 1, "gen": t(minutes=-1), "hz": t(hours=12),
         "hl": "Calendar is clear until tomorrow and nobody is waiting on you.",
         "cards": [{"id": "cal:standup", "k": "event", "p": 30, "at": at(1, 9, 30), "t": "Standup",
                    "st": at(1, 6, 0)}]}

for name, doc in (("day", day), ("empty", empty)):
    with open(os.path.join(HERE, name + ".json"), "w") as f:
        json.dump(doc, f, indent=1, ensure_ascii=False)
print("now =", int(NOW.timestamp()))
