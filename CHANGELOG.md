# Changelog

## 0.2.1
- Fixed Real driver gaps: the gaps now reach the cars' lap times (in 0.2.0 only the form did).
- Cheaper memory checks with Swofty Launcher 1.9.30 or newer (fewer small hitches).

## 0.2.0
- New setting **Real driver gaps** (off by default): team-mates are as far apart as in real qualifying, from the
  2026 averages (2025 with the 2025 line-up). Alonso is about 0.35 s quicker than Stroll, Norris and Piastri stay
  close. The car's pace stays the game's. Works with Form strength Off too, then only the gaps change.

## 0.1.5
- Fixed a small stutter every half second while driving: who drives each car is now checked every 10 seconds
  instead of every frame.
- Time Trial is left alone: no form for your ghosts or rival times.
- Fixed your own car getting a form for a few seconds at the start of a session.

## 0.1.4
- Faster start of a session: the grid is read with far fewer memory checks, and the slow table of driver initials
  is only read when the game has no name for a driver. Shorter pause on the loading screen.
- Shows as tested on Steam in the launcher's Addon Browser, in the AI category. Needs launcher 1.8.36 or newer.

## 0.1.3
- Fixed a burst of toasts after unpausing: mistakes, bad patches and hot spells now wait while the game is paused.
  Needs launcher 1.8.33 or newer.

## 0.1.2
- Fixed heavy lag when the addon can't read the grid: it kept retrying twice a second. It now backs off after a few tries.
- Fixed small frame spikes every 10 seconds.
- The driver names are read once per game run, so later sessions start faster.

## 0.1.1
- The source code is now public, under the MIT licence: https://github.com/constructor-mods/driver-form
- No changes in game.

## 0.1.0
- First release.
