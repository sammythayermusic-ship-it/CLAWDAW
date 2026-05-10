// Bridge from typed tokens to CSS custom properties on `:root`.
// Components style themselves with `var(--color-bg-glow)` etc., never with
// raw hex. If you find yourself reaching for a hex literal in a component,
// add a token to tokens.ts first, then re-run this.

import { tokens, color, glass, motion, elevation, radii, space, typography } from "./tokens";

function rgba(hex: string, opacity: number): string {
  const h = hex.replace("#", "");
  const r = parseInt(h.slice(0, 2), 16);
  const g = parseInt(h.slice(2, 4), 16);
  const b = parseInt(h.slice(4, 6), 16);
  return `rgba(${r}, ${g}, ${b}, ${opacity})`;
}

function setVar(root: HTMLElement, name: string, value: string | number) {
  root.style.setProperty(name, typeof value === "number" ? `${value}px` : value);
}

export function applyThemeToRoot(): void {
  const root = document.documentElement;

  // ---- Color
  setVar(root, "--color-bg-glow", color.background.glow);
  setVar(root, "--color-bg-warm", color.background.warm);
  setVar(root, "--color-bg-deep", color.background.deep);
  setVar(root, "--color-bg-shadow", color.background.shadow);

  setVar(root, "--color-surface-glass", color.surface.glass);
  setVar(root, "--color-surface-glass-raised", color.surface.glassRaised);
  setVar(root, "--color-surface-glass-edge", color.surface.glassEdge);
  setVar(root, "--color-surface-glass-inner-shadow", color.surface.glassInnerShadow);

  setVar(root, "--color-text-primary", color.text.primary);
  setVar(root, "--color-text-secondary", color.text.secondary);
  setVar(root, "--color-text-tertiary", color.text.tertiary);

  setVar(root, "--color-accent-active", color.accent.active);
  setVar(root, "--color-accent-hover", color.accent.hover);

  setVar(root, "--color-meter-low", color.meter.low);
  setVar(root, "--color-meter-mid", color.meter.mid);
  setVar(root, "--color-meter-high", color.meter.high);
  setVar(root, "--color-meter-peak-hold", color.meter.peakHold);

  setVar(root, "--color-agent-indicator", color.agentIndicator);

  // ---- Typography
  setVar(root, "--font-display", typography.family.display);
  setVar(root, "--font-text", typography.family.text);
  setVar(root, "--font-mono", typography.family.mono);

  for (const [name, def] of Object.entries(typography.scale)) {
    setVar(root, `--type-${name}-size`, def.size);
    setVar(root, `--type-${name}-line-height`, String(def.lineHeight));
    setVar(root, `--type-${name}-weight`, String(def.weight));
    setVar(root, `--type-${name}-tracking`, `${def.tracking}em`);
  }

  // ---- Radii
  setVar(root, "--radius-control", radii.control);
  setVar(root, "--radius-card", radii.card);
  setVar(root, "--radius-panel", radii.panel);
  setVar(root, "--radius-pill", radii.pill);

  // ---- Glass
  setVar(root, "--glass-blur", glass.blurPx);
  setVar(root, "--glass-surface-alpha", String(glass.surfaceAlpha));
  setVar(root, "--glass-edge-highlight", rgba(glass.edgeHighlight.color, glass.edgeHighlight.opacity));
  setVar(
    root,
    "--glass-inner-shadow",
    `inset 0 ${glass.innerShadow.offsetY}px ${glass.innerShadow.blur}px ${rgba(glass.innerShadow.color, glass.innerShadow.opacity)}`,
  );

  // ---- Spacing
  for (const [name, value] of Object.entries(space)) {
    setVar(root, `--space-${name}`, value);
  }

  // ---- Motion
  setVar(root, "--motion-hover", `${motion.duration.hover}ms`);
  setVar(root, "--motion-press", `${motion.duration.press}ms`);
  setVar(root, "--motion-panel-slide", `${motion.duration.panelSlide}ms`);
  setVar(root, "--motion-ambient", `${motion.duration.ambient}ms`);
  setVar(root, "--ease-standard", motion.easing.standard);
  setVar(root, "--ease-spring", motion.easing.spring);
  setVar(root, "--ease-gentle", motion.easing.gentle);

  // ---- Elevation — pre-composed shadows for direct use in box-shadow.
  setVar(
    root,
    "--elevation-rest",
    `0 ${elevation.rest.offsetY}px ${elevation.rest.blur}px ${rgba(elevation.rest.color, elevation.rest.opacity)}`,
  );
  setVar(
    root,
    "--elevation-hover",
    `0 ${elevation.hover.offsetY}px ${elevation.hover.blur}px ${rgba(elevation.hover.color, elevation.hover.opacity)}`,
  );
  setVar(
    root,
    "--elevation-active",
    `inset 0 ${elevation.active.offsetY}px ${elevation.active.blur}px ${rgba(elevation.active.color, elevation.active.opacity)}`,
  );
}

export { tokens };
