# r/unixporn submission

Ready to paste. Everything below the rules is final text: no em dashes, no
tells, nothing to edit before it goes out.

## What to submit

Post `docs/media/aquarium.mp4` (1280x800, 8 s, h.264, no audio) as the
submission itself. r/unixporn is an image and video subreddit, and a link post
to a repo gets removed.

The motion is the whole differentiator, so the clip beats any screenshot. If
you would rather lead with a desktop than a background, use
`apple-glass/docs/media/01-hero.png` instead and swap the theme line in the
details comment to Apple Glass.

## Title

    [Hyprland] My desktop background is one fragment shader

Alternates, same shape (`[WM] short claim`, no marketing voice):

    [Hyprland] Every fish here is a few lines of GLSL
    [Hyprland] Live aquarium background, no video and no images

## Body

> Everything in the water is one fragment shader on a wlr-layer-shell surface,
> sitting above the wallpaper and below every window. The light shafts, the
> caustics on the sand, the weed, the boulders, the bubbles, the fish, the
> jellyfish. No video, no sprites, no scene graph.
>
> The fish dodge the mouse pointer, and the whole tank flinches when a desktop
> notification arrives.
>
> Source: https://github.com/macarchy/omarchy-aquarium (MIT)

## Details comment

Post this as your own top-level comment right after submitting. The missing
details comment is the rule people actually get removed for.

> * Distro: Arch (Asahi) on a MacBook Pro 13-inch M2
> * WM: Hyprland, via Omarchy
> * Background: omarchy-aquarium, https://github.com/macarchy/omarchy-aquarium
> * Theme: Apple Glass Light, https://github.com/macarchy/apple-glass-light
> * Terminal: kitty
>
> The clip is the default palette. Passing `--theme` tints the water from your
> own colours instead, so the tank follows whatever theme you run.
>
> It only needs wlr-layer-shell and GLES2, so it should run on Sway, river and
> Wayfire too. Hyprland is the only one I can test on, and I would like to hear
> how it goes elsewhere.

## If someone asks

Battery: it suspends completely behind a fullscreen window, tearing down the
EGL surface rather than just skipping frames, and it caps its rate on battery.
The measured per-frame cost is in the repo README.

Do not lead with the performance work. This audience wants the picture first.
The engineering belongs in a reply, if someone asks for it.
