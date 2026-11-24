# Drum Machine

A drum sequencer: each track is an instrument, each step a sixteenth note. Notes go out as **MIDI** on
channel 10 to the system's General MIDI synth (SoundFont).

## Composing

- **Click** a step: a note; click again to remove it.
- **Right click** or **Shift+click**: an **accent** (louder note, marked with a dot).
- **Drag**: paints or erases several steps at once.
- **Space**: play and stop.

## The instruments

- The grid has a row for each of the **kit's 61 instruments** (General MIDI and GS), grouped by family.
  Click a family's header to collapse or expand it; the wheel scrolls.
- **Compact** (above the names, or View, Compact view) shows only the basic kit and the rows that play;
  **All** shows them all.
- **Click a name**: listen to the instrument; **right click**: its menu (clear, mute, solo, level).
- The **level bar** sets its strength; **M** mutes it, **S** plays it solo.

## Bars and time signature

- **Time signature**: 3/4, 4/4, 5/4, 6/8 or 12/8; **Bars**: 1 to 8.
- The **1, 2, 3…** tabs above the grid show one bar; **Follow playback** makes the grid follow;
  **Copy to following bars** copies the bar shown; Page Up and Page Down change bars.

## Sound

- **Kit**: the drum kits of the SoundFont (Standard, Room, Power, Electronic, 808/909, Jazz, Brush...).
- **Swing** delays the off-beats; **Tempo** in quarter notes per minute.

## Files

- **Save** writes a `.drum` file (text). Examples are in `/home/public/DrumMachine`.
- **Export as MIDI** writes a `.mid` file (channel 10, tempo, time signature, kit).
