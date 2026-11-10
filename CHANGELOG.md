# Changelog

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
