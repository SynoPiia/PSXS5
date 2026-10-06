# User interface

This template stops at a graphical Hello World on purpose: the root app draws
text and shapes on the CPU through VideoOut
([`src/demo_renderer.hpp`](../src/demo_renderer.hpp)), which is enough to prove
that a build starts, draws and reads a controller. It is not a way to build a
real interface, and the template does not try to become one.

When your app needs menus, lists, dialogs, settings screens or anything a
player navigates, use the companion project:

**[ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui)**:
a reference and a reusable kit for console-grade PS5 homebrew interfaces,
drawn with OpenGL 4.6.

## What it gives you

| | |
| --- | --- |
| A renderer | One instanced signed-distance-field shader for every shape, glyph and image; animated procedural backdrops; frosted glass |
| Components | About a hundred reusable pieces that own their focus, motion and sound: lists, grids, carousels, tabs, menus, dialogs, notifications, forms, pickers, an on-screen keyboard, tables, charts, media controls, HUD pieces, layout and spatial focus navigation |
| Themes | Thirty design languages as plain data (frosted glass, neo-brutal, neumorphic, 8-bit, hand-drawn, and looks modelled on well-known web frameworks); every component works in all of them |
| Motion and sound | Springs and easing, a 32-voice mixer with a cue vocabulary, two sets of recorded interface sounds, music with ducking, controller rumble |
| Finished screens | Complete designs to read and copy from: a home screen, a library, a store, a settings screen, a pause menu, a file browser and more |
| A PC preview | The same interface code renders on Linux or WSL to PNG frames, so you (or a coding agent) iterate without a console |
| Documentation | A craft guide, an API reference, one page per component group, and an `AGENTS.md` for AI coding agents |

It has been run on PS5 hardware at 3840 x 2160, every screen holding 60
frames per second; its README gives the measured numbers and says what was
not verified.

## How it relates to this template

ps5-homebrew-ui is itself built on this boilerplate: the same Make targets,
the same native converter and FSELF writer, the same runtime shim and the
same `sce_sys/` layout. What it adds on top is the
[ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) SDK (linked
through `APP_STATIC_ARCHIVES`, as any other static library here), the kit
itself and its assets.

So there are two ways to use it:

1. **Start from ps5-homebrew-ui** when you are beginning an app with an
   interface. You get this template's build and packaging and the kit
   together. Set your identity with `make init`, delete the sample designs you
   do not want, and build your screens. Its
   [ADOPTING.md](https://github.com/blackbearreloaded/ps5-homebrew-ui/blob/main/docs/ADOPTING.md)
   ("Start from this app") lists the steps.
2. **Add the kit to an app you already started from this template.** Copy its
   `src/gfx`, `src/ui`, `src/core`, `src/audio` and `src/platform/ps5`
   directories and its baked fonts and sounds, and link ps5-opengl. The same
   ADOPTING.md ("Take the kit") says which directory needs what and shows the
   smallest program that draws with it, and records what real adoptions
   taught.

Either way the kit replaces the CPU demo renderer; the two are not meant to
draw into the same frame.

## Working well together

- **Update notices.** The [update check](UPDATE_CHECK.md) here answers "is
  there a newer release?" and deliberately draws nothing. The kit's
  `ui::NotificationStack` is the matching interface: a notification with
  "Update now" and "Later" that can stay until the player answers, with
  progress and a history. Its
  [notifications guide](https://github.com/blackbearreloaded/ps5-homebrew-ui/blob/main/docs/components/notifications.md)
  has the recipe.
- **Storage.** The kit keeps settings under `/download0`, with the same
  write-then-rename discipline as the [storage recipe](RECIPES.md).
- **Home-screen assets.** Both projects use the `sce_sys/` layout described
  in [Presentation assets](PRESENTATION_ASSETS.md).
- **Clean exit.** The kit's app ends through
  `sceSystemServiceLoadExec("exit", NULL)` and offers a self-driving run for
  hardware checks; see its
  [console validation guide](https://github.com/blackbearreloaded/ps5-homebrew-ui/blob/main/docs/CONSOLE_VALIDATION.md).

## What to know before choosing it

- It needs OpenGL through ps5-opengl. If your app renders another way
  (VideoOut directly, SDL's CPU renderer, your own engine), the kit's ideas
  and its craft guide still apply, but its code does not drop in.
- It is C++20 without exceptions or RTTI, like this template.
- It is licensed GPL-3.0-or-later, as this template is. Its fonts and other
  third-party parts keep their own licences; its artwork, sounds and music
  belong to that project. Read its `THIRD_PARTY_NOTICES.md` before shipping
  them in your own app.
- It is a separate repository with its own releases. This template does not
  vendor it and stays small; nothing here depends on it.
