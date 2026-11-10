# Click Frames

Shows how many frames every click of your run is held for, right on screen.

Every time you press and let go of the jump button, the mod measures how many
update steps (frames) your click lasted and puts it into a coloured counter row
in the corner. A circle with the same number is drawn where you clicked.

## What it does

- **Counter rows** - `1, 2, 3, 4, 5-6, 7-8, 9-12, 13-16, 17+` by default, each
  row in its own colour. Fully configurable, including the colours.
- **Click circles** - a coloured dot plus the frame count at every click.
  Optionally fades out after a few seconds.
- **Last click readout** - a small line with the newest click.
- Circles and counts are cleared when you restart the level.

## How frames are counted

A frame here is one update step of the level, i.e. the real physics rate the
game runs at. On 60 FPS that is 60 frames per second; if you use a TPS bypass
(Eclipse: Physics Bypass, for example) set to 240, a frame is 1/240 of a second.
The mod does not change your physics rate and does not need any bypass to work.

The count is measured live, not from a macro: this is a helper for looking at
your own clicks, not an analyzer and not a bot. Nothing is recorded, uploaded
or played back.

## Notes

- Settings are read when a level starts, restart the level after changing them.
- Works in normal mode and practice mode; clicks are measured for the player
  that pressed the button.
- This is an original mod inspired by the idea of frame counters. It shares no
  code with FB Counter and has no connection to fbmod.com.

## Credits

- Idea of frame counters: FB Counter by Electro Reptile (inspiration only)
