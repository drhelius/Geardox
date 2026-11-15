# Geardox Shaders

Geardox desktop shader presets use `.gshader` files. A preset is an INI-style file that describes one or more GLSL fragment shader passes. Presets placed in this `shaders` directory, up to 64, are discovered once when the desktop app starts and appear in the `Video > Shader` menu after `Pixel Perfect`. Shader presets are not applied while the debugger is enabled.

GLSL files are normal fragment shaders. Geardox supplies a fullscreen vertex shader and prepends the correct GLSL version, so shader files should not include a `#version` line.

## Minimal Preset

```ini
[Preset]
Name=Scanlines + Bilinear
Passes=1

[Pass0]
Path=scanlines.glsl
ScaleType=Viewport
Filter=Linear

[Parameters]
ScanlineIntensity=0.10

[Parameter.ScanlineIntensity]
Label=Intensity
Min=0.0
Max=1.0
Step=0.01
```

## Preset Sections

`[Preset]`

- `Name`: display name shown in the menu. If omitted, the filename without its extension is used.
- `Group`: optional submenu name. Presets with the same group are listed together in that submenu.
- `Passes`: number of passes, from 1 to 32.
- `Variants`: optional number of `[VariantN]` sections, up to 16.
- `DefaultVariant`: variant applied when the preset is loaded for the first time. Defaults to `0`.

`[PassN]`

- `Path`: GLSL fragment shader path. Relative paths are resolved from the `.gshader` file directory first, then from the `shaders` resource directory.
- `Alias`: optional name for the pass output. Later passes can sample it with that name. See Shader Inputs.
- `Filter`: `Nearest` by default. Use `Linear` or `Bilinear` for bilinear sampling on that pass input/output texture.
- `Feedback`: `true` when the pass needs the previous frame through `PassFeedback0`.
- `History`: `true` when the pass needs previous inputs to the same pass through `SourceHistory0` through `SourceHistory7`.
- `ScaleType`: sets both axes. Defaults to `Viewport`. Values are:
  - `Source` (or `Original`): relative to the original emulator frame, at native core resolution.
  - `Previous`: relative to the previous pass output. This is RetroArch's `source` scale type.
  - `Viewport` (or `FinalViewport`): relative to the final display viewport.
  - `Absolute`: fixed size from `AbsoluteWidth` and `AbsoluteHeight`.
- `ScaleTypeX`, `ScaleTypeY`: optional per-axis scale type overrides.
- `Scale`: output scale for both axes. Defaults to `1.0`.
- `ScaleX`, `ScaleY`: optional per-axis scale overrides.
- `AbsoluteWidth`, `AbsoluteHeight`: used with `ScaleType=Absolute`.
- `FloatFramebuffer`: `true` when the pass output needs signed or high-range intermediate values. Uses a floating-point render target.

The final pass is what Geardox displays. Non-final passes render to intermediate textures. Only the final pass needs
`Viewport`; passes that work at core resolution should use `Source` or `Previous` to avoid rendering at display size.

`[Parameters]`

Each entry creates a float uniform with the same name:

```ini
[Parameters]
Amount=0.5
```

Optional metadata for sliders:

```ini
[Parameter.Amount]
Label=Amount
Min=0.0
Max=1.0
Step=0.01
```

`Label` defaults to the parameter name. `Min`, `Max` and `Step` default to `0.0`, `1.0` and `0.01`, and the `[Parameters]`
value is clamped to the `Min` to `Max` range.

A parameter with `Min=0.0`, `Max=1.0` and a `Step` of `1.0` or higher is displayed as a checkbox. Other parameters with
a `Step` of `1.0` or higher are displayed as integer sliders.

Optional `Group` puts the parameter in a collapsible section of the Shader Parameters window, which opens from
`Video > Shader > Parameters...`:

```ini
[Parameter.Amount]
Label=Amount
Group=Glow
```

A preset can expose up to 256 parameters.

`[VariantN]`

A variant is a named set of parameter values. Selecting it resets every parameter to its `[Parameters]` value and then
applies the variant values. Variants are listed as submenus of the preset in `Video > Shader` and in the Shader
Parameters window. `Restore Defaults` restores the selected variant.

```ini
[Preset]
Name=My CRT
Variants=2

[Variant0]
Name=Slot Mask

[Variant1]
Name=Aperture Grille
MaskType=2.0
```

## Shader Inputs

Available samplers:

- `uniform sampler2D Source`: current pass input.
- `uniform sampler2D Original`: original emulator frame, the active picture of the video card without its border.
- `uniform sampler2D PassFeedback0`: previous final output, for feedback presets.
- `uniform sampler2D SourceHistory0` through `SourceHistory7`: previous inputs to the same pass, newest first, for history presets.
- `uniform sampler2D PassOutputN`: output of any previous pass, where `PassOutput0` is pass 0 output, `PassOutput1` is pass 1 output, and so on.
- `uniform sampler2D <Alias>`: output of a previous pass with that `Alias`.
- `uniform sampler2D <Alias>Feedback`: previous frame output of the non-final pass with that `Alias`. A pass can sample its own feedback.

A pass can use up to 16 samplers.

Available size uniforms are `vec4(width, height, 1.0 / width, 1.0 / height)`:

- `SourceSize`: current pass input size.
- `OriginalSize`: original emulator frame size. It follows the video mode, so it changes whenever the program
  switches modes, up to the maximum frame buffer size defined in `src/common/defines.h`. With `Video > Native
  Resolution` on, which is the default, `Original` has one texel for each pixel the program addresses, such as
  `320x200` in VGA mode 13h. With it off, `Original` keeps every monitor line, and modes whose pixels are wider than
  one dot are also expanded to one texel per dot when the result fits the frame buffer.
- `OutputSize`: current pass output size.
- `FinalViewportSize`: final display viewport size.
- `PassFeedback0Size`: feedback texture size.
- `SourceHistorySize`: source history texture size.
- `PassOutputSizeN` (or `PassOutputNSize`): previous pass output texture sizes.
- `<Alias>Size`: output size of the pass with that `Alias`.

Other uniforms:

- `FrameCount`: increments once per new emulated frame. It starts at `0` when a preset is loaded.
- `FrameDirection`: `1` normally, `-1` while rewinding.
- `SourceHistoryCount`: number of valid source history textures, from `0` to `8`.
- `OriginalAspect`: display width / display height of the picture, counting monitor dots and scanlines at full
  resolution, for example `640 / 400` for 320x200 modes. It does not change with Native Resolution.
- `FramebufferScale`: display pixel density, as the rounded ratio between framebuffer pixels and window points, at
  least `1.0`. Use it to keep masks and grids at a fixed physical size on high-density displays.
- `BackgroundColor`: configured normal desktop background color as `vec4(r, g, b, 1)`.

PC video uniforms, updated every frame:

- `float OriginalFPS`: refresh rate of the current video mode in Hz, which comes from the CRTC timing that the video
  BIOS or the program sets: `59.92` on the CGA and in the EGA 200-line modes, about `59` to `60` in the EGA 350-line
  modes (about `50` in its monochrome modes), `70.09` in the VGA 350 and 400-line modes, text modes included, and
  `59.94` in the 480-line modes. The Cirrus SVGA modes follow the clock and timing their mode programs. `60.0` while
  the video is not running.
- `float PixelClock`: rate of the `Original` texels in MHz, which is the CRTC dot clock divided by the dots each texel
  covers. With `Video > Native Resolution` on, a texel is a pixel as the program addresses it: `12.588` in the VGA
  320x200 modes, `25.175` in the VGA 640-pixel graphics modes, `28.322` in 720-pixel VGA text, `14.161` in VGA
  40-column text, `7.159` in the CGA and EGA 320x200 modes and 40-column text, `14.318` in CGA 80-column text and
  640x200, and `16.257` in the EGA 640x350 modes. With it off, a mode that fits the frame buffer width has one texel per
  dot and reports the dot clock, such as `25.175` in the VGA 320x200 modes. CGA composite always reports `14.318`: four
  texels make one cycle of the `3.579` MHz color subcarrier. The Cirrus SVGA modes use the clock their mode programs.
- `float LineRate`: horizontal scan rate in kHz, the dot clock divided by the dots in a line. It is `15.70` on the CGA
  and in the EGA 200-line modes, about `21.8` in the EGA 350-line modes (`18.3` to `18.4` in its monochrome modes),
  and `31.47` in all the standard VGA modes. The Cirrus SVGA modes can go higher.
- `float SourceLines`: number of scanlines a scanline shader draws, one per `Original` row, so it always equals
  `OriginalSize.y`. With `Video > Native Resolution` on, the VGA 200-line modes such as mode 13h have `200` rows, one
  per pixel, although the card scans each row twice; with it off they have `400`, one per monitor line.
- `vec4 BorderColor`: color of the border around the picture as `vec4(r, g, b, 1)`, which `Original` does not include.
  On the CGA it is the border color of the color select register, one of the 16 CGA colors. On the EGA, VGA and
  Cirrus it is the attribute controller's overscan color, through the palette. `vec4(0.0)` while the video is not
  running.
- `int SystemType`: video card, `0` IBM VGA, `1` Cirrus GD5428 on the ISA bus, `2` Cirrus GD5428 on the VL-Bus,
  `3` IBM CGA, `4` IBM EGA, `5` Cirrus GD5446 on the PCI bus. It follows the machine configuration, so it is valid
  even while the video is not running.
- `int VideoMode`: `0` text mode, `1` graphics mode. `0` while the video is not running.
- `int CoreFlags`: `4` when the CGA outputs composite, so the NTSC artifact colors are already in `Original` and an
  NTSC shader should not add them again; the bundled NTSC shaders skip their artifacts then. This is
  `Video > CGA Output > Composite Monitor`, or `Auto` with a 640x200 mode that has the color burst on. `0` otherwise.

While the video is not running, such as at power on before the BIOS programs the card, and while the key mapper is
open, `PixelClock`, `LineRate`, `BorderColor`, `VideoMode` and `CoreFlags` are `0`.

`FrameCount`, `SourceHistory`, `PassFeedback0` and `<Alias>Feedback` only advance when the emulator produces a new
frame, or when rewinding shows an earlier one. Redraws while paused, after a window resize or after a parameter change
render the same frame again with the same state.

Any parameter declared in `[Parameters]` is also available as a `float` uniform.

## Minimal GLSL Shader

```glsl
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D Source;

void main()
{
    FragColor = texture(Source, vTexCoord);
}
```

## Feedback Example

```ini
[Preset]
Name=Feedback Blend
Passes=1

[Pass0]
Path=feedback_blend.glsl
ScaleType=Viewport
Filter=Nearest
Feedback=true

[Parameters]
FeedbackAmount=0.5
```

A feedback shader can sample the previous final frame. Save this one as `feedback_blend.glsl` next to the preset:

```glsl
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D Source;
uniform sampler2D PassFeedback0;
uniform float FeedbackAmount;

void main()
{
    vec3 current = texture(Source, vTexCoord).rgb;
    vec3 previous = texture(PassFeedback0, vTexCoord).rgb;
    FragColor = vec4(mix(current, previous, FeedbackAmount), 1.0);
}
```
