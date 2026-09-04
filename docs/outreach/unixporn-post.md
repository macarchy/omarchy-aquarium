# Draft: r/unixporn submission

**Not posted.** Submit the video, not a screenshot — the whole point is that it
moves.

**Media to attach:** `docs/media/aquarium.mp4` (1280x800, 8 s, h.264, no audio).
Upload it as the submission itself; r/unixporn is an image/video subreddit and
a link post to a repo will be removed.

---

## Title

    [Hyprland] My desktop background is one fragment shader

Alternates, same rules (`[WM] short claim`, no marketing voice):

    [Hyprland] Every fish here is a few lines of GLSL
    [Hyprland] Live aquarium background — no video, no images, one shader

## Body

> Everything you can see in the water — the light shafts, the caustics on the
> sand, the weed, the boulders, the bubbles, the fish, the jellyfish — is one
> fragment shader on a `wlr-layer-shell` surface, above the wallpaper and below
> every window. No video, no sprites, no scene graph.
>
> The fish also dodge the mouse pointer, and the whole tank flinches when a
> desktop notification arrives.
>
> Shader and renderer: https://github.com/macarchy/omarchy-aquarium (MIT)

## Details comment

r/unixporn wants the setup spelled out; post this as your own top-level comment
right after submitting.

> - **Distro:** Arch (Asahi), on a MacBook Pro (13-inch, M2)
> - **WM:** Hyprland, via Omarchy
> - **Background:** omarchy-aquarium — https://github.com/macarchy/omarchy-aquarium
> - **Theme:** Apple Glass Light — https://github.com/macarchy/apple-glass-light
>   (the clip is the default palette; `--theme` tints the water from your
>   colours instead, so the tank follows whatever theme you run)
> - **Terminal:** kitty
>
> It needs only `wlr-layer-shell` and GLES2, so it should run on Sway, river,
> Wayfire and friends as well — I only have Hyprland to test on, and I would
> like to hear how it goes elsewhere.

## Notes before posting

- Check the current r/unixporn rules first; they change, and the details-comment
  requirement is the one people get removed for.
- Do not lead with the performance work. This audience wants the picture; the
  engineering is for a comment if someone asks.
- Answer "does it eat your battery" honestly: it suspends behind fullscreen
  windows and caps its frame rate on battery, and the measured cost is in the
  repo's README.
