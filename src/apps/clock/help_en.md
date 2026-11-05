# Clock

The **Clock** shows the time on an analogue face and in digits, and keeps your **alarms**. Open it from the Korin OS menu, **Accessories** category. Two tabs at the top left: **Clock** and **Alarms**.

## The Clock tab

The face, the time in digits below it, and three buttons to set the time by hand: **+1 h**, **+1 min** and **−1 min**. The setting is kept in `SD0:/etc/clock.cfg`; with a network, the time comes from the Internet (NTP) by itself.

## The Alarms tab

- At the top, the **next alarm**: "Next alarm: tomorrow 07:00 — in 9 h 12 min", or "Snoozed until 07:10" after a snooze.
- The list: the time, name, repeat and state (On, Off, Snoozed, Missed) of each alarm, by time.
- **Add** opens the dialog for a new alarm (20 at most).
- **Edit**, or a double click on a row, opens the selected alarm.
- **Turn on** / **Turn off** switches it on or off.
- **Delete**, or the Delete key, removes it, without asking.

### The alarm dialog

| Field | Role |
|---|---|
| Time | hours and minutes (+ and − buttons) |
| Name | 40 characters at most; empty: "Alarm" |
| Repeat | Once, Every day, Weekdays, Weekends, or Custom days (the seven boxes below) |
| Sound | Chimes, Beeps or Marimba; **Try** plays one round |
| On | on or off |

A "once" alarm rings the next time its time comes (today, otherwise tomorrow), then turns itself off.

## When an alarm rings

The **clockd** service rings the alarms, even with the Clock closed: a notification at the top right, a card above the windows, and the sound in a loop.

- **Snooze for 10 min** (or Enter): it rings again 10 minutes later.
- **Stop** (or Esc): it goes quiet; a "once" alarm turns off.
- With no answer within 2 minutes, the sound stops and "Missed alarm" is notified; the row says **Missed**.

If the Pi was off when a "once" alarm was due (within the last 12 hours), it is notified as missed at startup, without sound. Alarms do not ring during the first minute after startup, until the time is sure.

## The file

The alarms are in `SD0:/etc/alarms.txt`, in Onyx's format: one `[alarm]` block per alarm, `key = value` lines. The `snooze = 10` line at the top gives the snooze minutes (1 to 30).
