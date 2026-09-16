# NÁCAR — implementation contract

Read this before touching any file. It is short on purpose.

## Where things are

```
/home/user/NACARVST            project root
/home/user/JUCE                JUCE 8.0.4 checkout (read-only reference)
DesignReference/NACAR_UI_SPEC.md   transcription of the locked reference image
Source/UI/Theme.h              every colour, font and shading routine
Source/UI/Layout.h             every coordinate
Source/UI/Components/Widgets.h shared widget vocabulary
Source/UI/Components/Icons.h   every glyph
Source/UI/EditorHost.h         what a panel may ask of the editor
Source/Plugin/ParameterList.h  every parameter, declared once
Source/Plugin/ParameterRegistry.h  how to read and write them
```

CMake globs `Source/**/*.cpp`, so a new file in `Source/` is picked up with no
build-script change.

## Frozen files

Do **not** edit these. They are the contract other work is being written
against in parallel:

```
Source/UI/Theme.h              Source/UI/Layout.h
Source/UI/Components/Widgets.h Source/UI/Components/Icons.h
Source/UI/EditorHost.h         Source/Plugin/ParameterList.h
Source/Plugin/ParameterRegistry.h  Source/Plugin/PluginProcessor.h
Source/Plugin/PluginEditor.h   Source/Plugin/StateManager.h
Source/Audio/Sources/Synth/SynthEngine.h
CMakeLists.txt
```

If something you genuinely need is missing from one of them, do not add it —
work around it inside your own files and say so in your report.

## Conventions

- Namespace: `nacar::ui` for interface code, `nacar` for audio and plugin code.
- Region components take `(NacarProcessor&, EditorHost&)` and draw in
  **region-local** coordinates. The `layout::` sub-namespaces are already
  region-local, so `layout::mut::mutateButton` is relative to the mutate panel's
  own origin, not to the window.
- Colours come from `theme::` only. Never construct a `juce::Colour` from a
  literal in a component.
- Coordinates come from `layout::` only. If a number describes *where something
  is*, it belongs in `Layout.h` — and `Layout.h` is frozen, so if you need one
  that isn't there, define it as a `static constexpr` at the top of your own
  `.cpp` with a comment saying why.
- Fonts come from `theme::display/medium/label/mono`. Tracked (letter-spaced)
  runs go through `theme::drawTracked` or `ui::ceramicLabel` / `ui::glassLabel`.

## JUCE 8 notes

- `juce::Font` must be constructed through `juce::FontOptions`. The old
  `Font (height)` and `Font (name, height, style)` constructors are deprecated
  and warn. Use the `theme::` helpers instead of constructing fonts directly.
- `Graphics::drawText` with a `Rectangle<float>` is fine; `Justification` flags
  are the JUCE ones.
- `juce::Component::setBounds` takes ints. Use `.toNearestInt()` on the layout
  rectangles.

## Realtime contract (audio code only)

Inside anything reachable from `processBlock`: no heap allocation, no locks, no
file IO, no logging, no `juce::String`. Allocate in `prepare()`, index into
preallocated storage afterwards.

## Honesty

If you leave something incomplete, say so in your report and mark it in the code
with a comment that names what is missing and how it would be finished. Do not
describe a stub as working. Do not claim you compiled anything — you are not
running the build; integration compiles happen centrally.
