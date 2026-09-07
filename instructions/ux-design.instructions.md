---
description: "Use when designing or reviewing any user interface — Terminal/Console TUIs, CSS, editor extensions, CLI/REPL output, or documentation layout. Covers user experience, usability, and graphic design principles: colour, typography, spacing, hierarchy, layout, Gestalt laws, interaction, feedback, accessibility, and the usability heuristics."
priority: reference
---

# Working with UX and Visual Design

A reference for the user experience, usability, and graphic design principles every interface in this project should follow. This guide has no `applyTo` file pattern because design principles are conceptual and cross-cutting — consult it whenever you design or review something a human looks at or interacts with: a Terminal or Console application, a CSS stylesheet, an editor-extension panel, REPL or CLI output, an error message, or a documentation page.

It is the _why_ behind the _how_. For implementation detail, pair it with the relevant guide: [css.instructions.md](css.instructions.md) for stylesheets, [luma.instructions.md](luma.instructions.md) for Luma applications, and [software-architecture.instructions.md](software-architecture.instructions.md) for the same simplicity-first mindset applied to code.

---

## Table of Contents

1. [Core Philosophy](#1--core-philosophy)
2. [How People Perceive and Think](#2--how-people-perceive-and-think)
3. [Nielsen's 10 Usability Heuristics](#3--nielsens-10-usability-heuristics)
4. [Gestalt Principles of Perception](#4--gestalt-principles-of-perception)
5. [Visual Hierarchy](#5--visual-hierarchy)
6. [Layout, Alignment, and Composition](#6--layout-alignment-and-composition)
7. [Whitespace and Spacing](#7--whitespace-and-spacing)
8. [Colour](#8--colour)
9. [Typography](#9--typography)
10. [Iconography](#10--iconography)
11. [Interaction Design](#11--interaction-design)
12. [Feedback and System Status](#12--feedback-and-system-status)
13. [Motion and Animation](#13--motion-and-animation)
14. [Error Prevention and Recovery](#14--error-prevention-and-recovery)
15. [Information Architecture and Navigation](#15--information-architecture-and-navigation)
16. [Consistency and Standards](#16--consistency-and-standards)
17. [Responsive and Adaptive Design](#17--responsive-and-adaptive-design)
18. [Accessibility and Inclusive Design](#18--accessibility-and-inclusive-design)
19. [Simplicity and Progressive Disclosure](#19--simplicity-and-progressive-disclosure)
20. [Anti-Patterns](#20--anti-patterns)
21. [Checklist](#21--checklist)

---

## 1 — Core Philosophy

Design serves the user, not the designer. Every visual and interaction decision should make the interface easier to understand and faster to use.

- **User-centred.** Design for the people who will actually use the product, their goals, and their context — not for your own convenience or taste.
- **Form follows function.** Appearance must serve purpose. Decoration that does not aid understanding or use is noise. Decide what something must _do_, then shape how it looks around that.
- **Simplicity first.** The best interface is the one with the least between the user and their goal. Remove before you add.
- **Intuitive and self-explanatory.** A good interface needs no manual. If users must be told how it works, the design — not the user — has failed.
- **Make the right thing easy.** Guide users toward success with sensible defaults, clear paths, and gentle constraints.
- **Respect the user's time and attention.** Do not make people wait, hunt, re-read, or remember what the system could surface for them.

**Test:** Before adding anything, ask — _does this help the user reach their goal, or does it get in the way?_

---

## 2 — How People Perceive and Think

Understanding human perception and cognition is the foundation of every other principle in this guide.

### Mental Models

A mental model is the user's internal picture of how something works. Interfaces succeed when they match the model the user already has.

- Mirror real-world concepts and conventions users already understand.
- Make objects behave consistently with their appearance — if it looks clickable, it must be clickable.
- A mismatch between the user's model and the system's behaviour is the root cause of most confusion.

### Cognitive Load

Cognitive load is the mental effort required to use an interface. Minimise it.

- **Reduce extraneous load:** strip away anything that does not help the user decide or act.
- **Chunk information** into small, meaningful groups rather than long undifferentiated lists.
- **Offload memory to the interface** — show, don't make people remember.

### Recognition Rather Than Recall

Recognising something is far easier than recalling it from memory.

- Show options rather than requiring users to remember and type them.
- Keep needed information visible or one click away, in context.
- Use familiar icons, labels, and patterns so meaning is recognised, not decoded.

### Useful UX Laws

| Law                          | What It Says                                                                 | Design Implication                                              |
| ---------------------------- | --------------------------------------------------------------------------- | -------------------------------------------------------------- |
| **Jakob's Law**              | Users spend most of their time on _other_ products.                         | Follow established conventions; do not reinvent the familiar.   |
| **Hick's Law**               | Decision time grows with the number and complexity of choices.              | Reduce and group options; reveal advanced choices on demand.    |
| **Fitts's Law**              | Time to hit a target depends on its size and distance.                      | Make important targets big and place them close to the action.  |
| **Miller's Law**             | People hold roughly 7 (±2) items in working memory.                         | Chunk content; do not rely on users remembering long lists.     |
| **Tesler's Law**             | Every process has irreducible complexity that lives somewhere.              | Absorb complexity into the system, not onto the user.           |
| **Postel's Law**             | Be liberal in what you accept, conservative in what you produce.            | Accept forgiving input; emit clear, predictable output.         |
| **Doherty Threshold**        | Productivity soars when system response is under ~400 ms.                    | Keep interactions fast; show progress when they cannot be.      |
| **Aesthetic-Usability**      | People perceive attractive designs as easier to use.                        | Visual polish builds trust and forgiveness — but never fakes function. |
| **Von Restorff Effect**      | The item that differs most is the one remembered.                           | Make the single most important element stand out — only one.    |
| **Serial Position Effect**   | People best remember the first and last items in a series.                  | Place key actions at the start or end of lists and menus.       |

---

## 3 — Nielsen's 10 Usability Heuristics

The ten heuristics are the canonical checklist for usability. Evaluate every interface against them.

| #   | Heuristic                              | In Practice                                                                                  |
| --- | -------------------------------------- | ------------------------------------------------------------------------------------------- |
| 1   | **Visibility of system status**        | Always keep users informed through timely feedback — loading, progress, saved, selected.     |
| 2   | **Match between system and real world**| Speak the user's language. Use familiar words, concepts, and ordering, not internal jargon.  |
| 3   | **User control and freedom**           | Provide clearly marked exits — undo, redo, cancel, back. Never trap the user.                 |
| 4   | **Consistency and standards**          | Follow platform and product conventions. The same word or control means the same thing.      |
| 5   | **Error prevention**                   | Design so mistakes cannot happen: constraints, confirmations, good defaults, forgiving input. |
| 6   | **Recognition rather than recall**     | Make objects, actions, and options visible. Do not force users to remember information.       |
| 7   | **Flexibility and efficiency of use**  | Offer accelerators (shortcuts, defaults) for experts without blocking novices.               |
| 8   | **Aesthetic and minimalist design**    | Show only what is relevant. Every extra element competes with the essential ones.            |
| 9   | **Help users with errors**             | State problems in plain language, diagnose the cause, and suggest a concrete recovery.       |
| 10  | **Help and documentation**             | Prefer interfaces that need no help; when help is needed, make it searchable and task-focused.|

> **Plain language matters most.** Across heuristics 2, 5, and 9, the single highest-leverage habit is to **use simple language** — short, concrete words a first-time user understands, never error codes or implementation terms.

---

## 4 — Gestalt Principles of Perception

Gestalt psychology describes how people group separate elements into a coherent whole. Use these principles deliberately to imply structure _without_ adding lines, boxes, or labels.

| Principle                     | What the Eye Does                                                        | Use It To                                                       |
| ----------------------------- | ----------------------------------------------------------------------- | -------------------------------------------------------------- |
| **Proximity**                 | Groups elements that are close together.                                | Group related items by spacing alone; separate unrelated ones. |
| **Similarity**                | Groups elements that look alike (colour, shape, size).                  | Signal that items share a role or category.                    |
| **Closure**                   | Completes incomplete shapes into recognisable forms.                    | Simplify icons and logos; the mind fills the gaps.             |
| **Continuity**                | Follows lines and curves, preferring smooth paths.                      | Align elements along a path to lead the eye through content.   |
| **Symmetry**                  | Perceives symmetrical elements as a unified, ordered group.             | Create balance and a sense of stability and quality.           |
| **Common Fate**               | Groups elements that move in the same direction.                        | Animate related items together so they read as one unit.       |
| **Common Region**             | Groups elements inside a shared boundary or background.                 | Use a card or panel to bind related controls together.         |
| **Connection (Connectedness)**| Groups elements joined by a line or connector most strongly of all.     | Link steps, nodes, or related fields with explicit connectors. |
| **Figure and Ground**         | Separates a focal object (figure) from its background (ground).         | Ensure the foreground is unambiguous; avoid ambiguous overlap. |
| **Good Figure (Prägnanz)**    | Interprets ambiguous images in the simplest, most stable way possible.  | Favour simple, regular forms — they are perceived and recalled fastest. |

The practical takeaway: **spacing and arrangement carry meaning**. Reach for proximity and common region before you reach for borders and dividers.

---

## 5 — Visual Hierarchy

Visual hierarchy guides the eye to the most important thing first, then the next, in the order that serves the user.

- **Establish one focal point (the hook).** Each screen or section should have a single dominant element that captures attention first. Competing focal points cancel each other out.
- **Encode importance with contrast.** Size, weight, colour, and spacing signal rank. Bigger, bolder, higher-contrast elements read as more important.
- **Use leading lines.** Implied or explicit lines — alignment edges, arrows, gaze direction, the flow of a layout — direct attention toward key content and calls to action.
- **Design for scanning, not reading.** People scan in predictable patterns: an **F-pattern** for text-dense pages, a **Z-pattern** for sparse, visual layouts. Place the most important elements along these paths.
- **Limit emphasis.** If everything is bold, nothing is. Reserve the strongest treatment for the one thing that matters most (the Von Restorff effect).

```text
Strong hierarchy                 Weak hierarchy
┌───────────────────────┐        ┌───────────────────────┐
│  Big Bold Headline    │        │ Headline              │
│  supporting subtext   │        │ supporting subtext    │
│                       │        │ another line          │
│  [  Primary action  ] │        │ [action] [action]     │
│   secondary link      │        │ [action] [action]     │
└───────────────────────┘        └───────────────────────┘
 one clear path                   no clear entry point
```

---

## 6 — Layout, Alignment, and Composition

Layout is the arrangement of elements in space. A strong layout feels ordered before a single word is read.

- **Align everything to a grid.** Shared edges create calm and structure. Every element should align to something — a column, a baseline, or another element.
- **Prefer few alignment lines.** Many different alignments look accidental; a small number looks intentional. Left-align body text for readability; centre only short, symmetrical content.
- **Group by proximity (see §4).** Place related controls together and separate unrelated groups with space.
- **Balance the composition.** Distribute visual weight so no region feels heavy or empty by accident. Symmetry conveys stability; deliberate asymmetry conveys energy.
- **Respect reading order.** In left-to-right locales the eye starts top-left; put primary content and actions where they will be seen first.
- **Use consistent containers.** Cards, panels, and sections (common region) make structure obvious and reusable.

---

## 7 — Whitespace and Spacing

Whitespace (negative space) is not empty — it is an active design tool that creates grouping, focus, and calm.

- **Whitespace is structure.** Generous space around an element draws attention to it. Tight space between elements binds them into a group (proximity).
- **Use a consistent spacing scale.** Derive every margin, padding, and gap from one base unit (commonly multiples of 4 or 8). Arbitrary, one-off spacing looks careless and is hard to maintain.
- **Spacing communicates relationships.** Space _within_ a group must be smaller than space _between_ groups, or the grouping reads wrong.
- **Give content room to breathe.** Adequate padding inside containers and around text improves comprehension and perceived quality.
- **Do not fear empty space.** Cramming more in rarely helps; it raises cognitive load and weakens hierarchy.

| Token  | Example Step | Typical Use                                |
| ------ | ------------ | ------------------------------------------ |
| `xs`   | 4 px         | Icon-to-label gaps, tight inline spacing   |
| `sm`   | 8 px         | Related controls, list-item padding        |
| `md`   | 16 px        | Default gap between elements               |
| `lg`   | 24 px        | Separation between groups                   |
| `xl`   | 32–48 px     | Section breaks, page margins                |

---

## 8 — Colour

Colour conveys meaning, sets mood, and builds hierarchy — but it must never be the _only_ carrier of meaning.

- **Use a restrained palette.** A common starting point is the **60-30-10 rule**: 60 % dominant/neutral, 30 % secondary, 10 % accent for emphasis and calls to action.
- **Assign colour by role, not by hue.** Define semantic tokens (`primary`, `surface`, `text`, `success`, `warning`, `danger`) rather than naming colours `blue` or `red`. This keeps meaning consistent and theming possible.
- **Ensure sufficient contrast.** Meet WCAG AA at minimum: **4.5:1** for normal text, **3:1** for large text and for UI component / graphical boundaries. High contrast is legibility, not decoration.
- **Never rely on colour alone.** Roughly 1 in 12 men has a colour-vision deficiency. Pair colour with an icon, label, shape, or text so the message survives in greyscale.
- **Mind contrast and emphasis together.** Reserve your most saturated accent for the single most important action; overuse drains its power.
- **Respect mood and convention.** Colours carry cultural and emotional associations (red = danger/stop, green = success/go). Use them consistently with user expectations.

---

## 9 — Typography

Type is the interface. Most products are mostly words, so typographic clarity is usability.

- **Establish a type scale.** Use a small set of sizes derived from a consistent ratio. A handful of well-chosen steps beats dozens of ad-hoc sizes.
- **Build hierarchy with size, weight, and spacing** — not colour alone. Headings, subheadings, body, and captions should be instantly distinguishable.
- **Optimise for readability.** Keep line length to ~45–75 characters (≈66 is ideal). Set body line-height around 1.4–1.6. Avoid long passages in all-caps or light weights.
- **Limit font families.** One or two families is plenty — typically one for headings and one for body, or a single family with multiple weights. More looks chaotic and slows loading.
- **Pair fonts with contrast and harmony.** Combine typefaces that differ clearly (e.g. a sturdy heading face with a neutral body face) yet share proportions.
- **Left-align long text.** Justified text creates uneven "rivers"; centred text is hard to scan beyond a line or two.
- **Use a monospace face for code.** Code, paths, and identifiers belong in a monospaced font so alignment and character distinctions are clear.

---

## 10 — Iconography

Icons aid recognition and save space — but only when their meaning is unambiguous.

- **Pair icons with text labels** wherever space allows. An icon alone is often ambiguous; a label removes doubt and aids recognition over recall.
- **Use conventional, recognisable symbols.** A magnifying glass means search; a trash can means delete. Do not invent novel metaphors for common actions.
- **Keep a consistent visual style.** One icon set, one stroke weight, one corner radius, one grid size. Mixed styles look unprofessional and slow recognition.
- **Size icons for their target.** Interactive icons need touch- and click-friendly hit areas (see §11), even when the glyph itself is small.
- **Give icons accessible names.** Provide a text alternative (label, `aria-label`, tooltip) so the meaning is available to assistive technology.

---

## 11 — Interaction Design

Interaction design governs how users act on the interface and how it responds.

- **Affordances and signifiers.** Make interactive elements look interactive. Buttons look pressable; links look clickable; draggable things look grabbable. The visual must signal the possible action.
- **Show all relevant states.** Every interactive element needs distinct **default, hover, focus, active, disabled,** and (where applicable) **selected/error** states. State changes confirm the system noticed the user.
- **Make targets easy to hit (Fitts's Law).** Use comfortable hit areas — at least ~44 × 44 px for touch — and place frequent actions where the pointer already is or where the eye expects them.
- **Prefer direct manipulation.** Let users act on objects directly (drag, resize, edit in place) rather than through indirect dialogs where it makes sense.
- **Keep visible focus.** Keyboard users must always see what is focused. Never remove focus indicators without a clear replacement.
- **Make actions reversible.** Favour undo over confirmation dialogs for routine actions; reserve confirmation for the genuinely destructive (see §14).

---

## 12 — Feedback and System Status

Every action deserves a reaction. Feedback closes the loop between intent and result and keeps the user oriented (heuristic #1).

- **Acknowledge every action immediately.** A press, save, selection, or submission must produce a visible or audible response within a perceptible moment.
- **Show system status.** Communicate loading, progress, success, and error states clearly. Users left guessing assume the system is broken.
- **Do not make users wait silently.** Response-time perception follows well-known thresholds:

| Delay        | User Perception                       | What To Do                                                      |
| ------------ | ------------------------------------- | -------------------------------------------------------------- |
| **≤ 0.1 s**  | Feels instantaneous                   | No indicator needed.                                            |
| **≤ 1 s**    | Noticeable, but flow is unbroken      | No spinner; keep the result snappy.                            |
| **1–10 s**   | Attention starts to wander            | Show a spinner or progress indicator; keep the user informed.  |
| **> 10 s**   | User likely switches tasks            | Show a progress bar with an estimate; allow cancel; signal completion. |

- **Prefer determinate progress.** A progress bar that shows how much is left beats an endless spinner.
- **Consider optimistic UI.** For high-confidence actions, reflect the result immediately and reconcile in the background — but always handle and surface failure.
- **Make feedback proportional.** Small actions get subtle feedback; large or destructive ones get prominent confirmation.

---

## 13 — Motion and Animation

Motion should clarify, not decorate. Good animation explains change; bad animation wastes time.

- **Animate with purpose.** Use motion to show relationships, maintain context across a transition, direct attention, or confirm an action — never purely for spectacle.
- **Keep it fast.** Micro-interactions ~100–200 ms; standard transitions ~200–300 ms; larger or more complex transitions up to ~500 ms. Slow animations make a product feel sluggish.
- **Use natural easing.** Ease-out for entering elements, ease-in for exiting, ease-in-out for moves. Avoid robotic linear motion for UI.
- **Animate cheap properties.** Prefer transform and opacity changes, which are smooth; avoid animating layout-affecting properties that cause jank.
- **Preserve continuity (common fate).** Move related elements together so the user can follow what changed and where things went.
- **Respect reduced-motion preferences.** Honour the user's "reduce motion" setting by disabling or softening non-essential animation.

---

## 14 — Error Prevention and Recovery

The best error is the one that never happens; the second best is one the user can undo in seconds.

- **Prevent errors first.** Use constraints, sensible defaults, input masks, disabled-until-valid actions, and clear formatting hints to make mistakes impossible or unlikely.
- **Confirm before destructive or irreversible actions.** Deleting, overwriting, or committing/publishing should require a deliberate confirmation — _"confirm before you commit."_ State plainly what will happen and make the safe choice the default.
- **Do not over-confirm.** Reserve confirmations for genuinely risky actions. For everything routine, prefer an easy **undo** instead of nagging dialogs.
- **Be forgiving of input (Postel's Law).** Accept varied formats (extra spaces, different date styles) and normalise them rather than rejecting the user.
- **Write helpful error messages.** Follow heuristic #9 — in plain language, say what went wrong, why, and exactly how to fix it. Never expose raw codes or stack traces to end users.
- **Always offer a way back.** Provide undo, cancel, and clearly marked exits so users never feel trapped by a mistake (heuristic #3).

```text
Unhelpful                          Helpful
┌──────────────────────────┐       ┌────────────────────────────────────────┐
│ Error 0x8007: invalid    │       │ That email address is missing an "@".  │
│ input.                   │       │ Example: name@example.com              │
│            [ OK ]        │       │            [ Fix it ]   [ Cancel ]     │
└──────────────────────────┘       └────────────────────────────────────────┘
```

---

## 15 — Information Architecture and Navigation

Information architecture (IA) is how content is organised, labelled, and connected. Good IA makes things findable; good navigation makes them reachable.

- **Organise around the user's tasks and mental model**, not your internal system structure or org chart.
- **Group and label clearly.** Use categories users recognise, with names that describe content honestly. Avoid clever or internal labels.
- **Keep hierarchy shallow.** Fewer levels and broader menus usually beat deep nesting. Most destinations should be a few clicks away.
- **Make location obvious.** Show users where they are (active states, breadcrumbs, titles), where they can go, and how to get back.
- **Provide consistent, predictable navigation.** Primary navigation should look and behave the same on every screen.
- **Support both browsing and searching.** Some users explore; others search. Offer both where the content volume justifies it.

---

## 16 — Consistency and Standards

Consistency lets users transfer what they learn from one part of the interface — or from other products — to the next.

- **Internal consistency.** The same action, label, icon, colour, and layout should mean the same thing everywhere in the product.
- **External consistency.** Follow platform and industry conventions (Jakob's Law). Users already know how a checkbox, a tab, or a back button works — do not redefine them.
- **Do not ignore standards.** Honour established UI patterns, platform guidelines, and accessibility standards. Deviate only with a clear, user-serving reason — novelty alone is not one.
- **Build and reuse a design system.** Shared tokens (colour, spacing, type) and components enforce consistency and speed up work. Define a pattern once; reuse it everywhere.
- **Be consistent in language too.** One term per concept. Do not call the same thing "delete" in one place and "remove" in another.

---

## 17 — Responsive and Adaptive Design

Interfaces must work across the range of screens, inputs, and contexts your users bring.

- **Design mobile-first.** Start from the smallest viewport and the core content, then progressively enhance for larger screens. It forces ruthless prioritisation.
- **Use fluid layouts.** Prefer relative sizing and flexible grids over fixed pixel widths so content adapts gracefully between breakpoints.
- **Prioritise content by context.** Show what matters most on small screens; reveal secondary content as space allows. Never simply hide essential functionality on mobile.
- **Design for both touch and pointer.** Provide large enough touch targets and spacing; do not hide critical actions behind hover, which touch devices lack.
- **Test real breakpoints and real content.** Check the layout at many widths and with long, short, and missing content — not only at idealised sizes.
- **Adapt, do not just shrink.** Reflow and reprioritise for the device; a cramped desktop layout squeezed onto a phone is not responsive.

---

## 18 — Accessibility and Inclusive Design

Accessible design is good design — it benefits everyone, and for many users it is the difference between usable and unusable.

- **Meet contrast minimums.** WCAG AA: 4.5:1 for text, 3:1 for large text and UI components (see §8).
- **Support full keyboard operation.** Every interactive element must be reachable and operable by keyboard, in a logical order, with a visible focus indicator.
- **Provide text alternatives.** Images, icons, and charts need descriptive alternative text so assistive technology can convey them.
- **Use semantic structure.** Proper headings, labels, roles, and landmarks let screen readers present content meaningfully. Label every form control.
- **Never rely on colour, shape, or position alone** to convey meaning — reinforce with text or icons.
- **Use plain language.** Clear, simple wording helps everyone, including users with cognitive differences or reading in a second language.
- **Respect user preferences.** Honour reduced-motion, increased-contrast, and font-size settings rather than overriding them.

---

## 19 — Simplicity and Progressive Disclosure

Simplicity is the discipline of showing only what is needed, when it is needed.

- **Minimise, then minimise again.** Every element on screen competes for attention. Remove anything that does not earn its place (heuristic #8).
- **Use progressive disclosure.** Show the common, essential options up front; tuck advanced or rare options behind "more", expanders, or secondary screens. This tames complexity without removing power.
- **Set smart defaults.** Most users never change defaults, so choose values that serve the majority and make the common path effortless.
- **Reduce choices (Hick's Law).** Fewer, clearer options speed decisions. Group and prioritise rather than presenting everything at once.
- **Declutter relentlessly.** Whitespace, fewer borders, and tighter copy almost always improve comprehension.
- **One primary action per view.** Make the main thing obvious and let secondary actions recede.

---

## 20 — Anti-Patterns

Avoid these common failures — each one directly violates a principle above.

- **No clear hierarchy.** Everything the same size and weight, so the eye has no entry point (§5).
- **Inconsistent patterns.** The same action styled or labelled differently across screens (§16).
- **Colour as the only signal.** Status shown by hue alone, invisible to colour-blind users and in greyscale (§8, §18).
- **Silent actions.** A click with no feedback, leaving users unsure whether anything happened (§12).
- **Walls of choices.** Long, ungrouped lists of options that overwhelm decision-making (§2, §19).
- **Cramped layouts.** Insufficient whitespace, so groups blur together and nothing breathes (§7).
- **Mystery-meat icons.** Unlabelled, unconventional icons users must guess at (§10).
- **Trapping the user.** No undo, no cancel, no obvious way back (§11, §14).
- **Cryptic errors.** Codes and stack traces instead of plain, actionable messages (§14).
- **Reinventing conventions.** Custom controls that ignore platform standards for no benefit (§16).
- **Decoration over function.** Animation, imagery, or styling that impresses but obstructs (§1, §13).
- **Making users wait blindly.** Long operations with no progress indication (§12).

---

## 21 — Checklist

- [ ] There is **one clear focal point** and a deliberate visual hierarchy on each view.
- [ ] Related items are **grouped by proximity and common region**; unrelated items are separated by space.
- [ ] **Alignment** is consistent — elements share a small number of edges and a grid.
- [ ] **Spacing** comes from a consistent scale; space within groups is smaller than space between them.
- [ ] **Colour** uses semantic roles, meets WCAG AA contrast, and is never the only signal.
- [ ] **Typography** uses a limited scale and ≤2 families; line length and line-height aid readability.
- [ ] **Icons** are conventional, consistent in style, and paired with labels or accessible names.
- [ ] Every interactive element shows **default, hover, focus, active, and disabled** states.
- [ ] Every action produces timely **feedback**; long waits show **progress** and never block silently.
- [ ] **Animation** is purposeful, fast, and respects reduced-motion preferences.
- [ ] Destructive or irreversible actions **confirm before committing**; routine actions offer **undo**.
- [ ] **Error messages** are in plain language and say how to recover.
- [ ] **Navigation** is consistent and shows the user where they are and how to get back.
- [ ] The interface follows **platform conventions and standards** — no needless reinvention.
- [ ] The layout is **responsive**, works for touch and pointer, and prioritises content on small screens.
- [ ] The interface is **keyboard operable**, with visible focus and text alternatives.
- [ ] Language is **simple and consistent** throughout.
- [ ] Anything that does not help the user reach their goal has been **removed**.
