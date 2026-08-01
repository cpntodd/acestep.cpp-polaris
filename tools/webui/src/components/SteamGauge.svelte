<script lang="ts">
	import { Cpu, Gauge, MemoryStick } from '@lucide/svelte';

	type GaugeKind = 'cpu' | 'gpu' | 'vram' | 'memory';
	type GaugeTone = 'green' | 'orange' | 'purple';

	let {
		label,
		value = null,
		detail = 'Telemetry unavailable',
		kind = 'gpu',
		tone = 'orange'
	}: {
		label: string;
		value?: number | null;
		detail?: string;
		kind?: GaugeKind;
		tone?: GaugeTone;
	} = $props();

	const ARC_LENGTH = 251.33;
	let percentage = $derived(
		value == null || !Number.isFinite(value) ? 0 : Math.max(0, Math.min(100, value))
	);
	let needleAngle = $derived(-90 + percentage * 1.8);
	let displayValue = $derived(
		value == null || !Number.isFinite(value) ? 'NO DATA' : `${Math.round(percentage)}%`
	);
</script>

<article
	class:unavailable={value == null}
	class="steam-gauge {tone}"
	aria-label={`${label}: ${detail}`}
	title={`${label}: ${detail}. This reading updates every few seconds.`}
>
	<div class="gauge-head">
		<span class="gauge-label">
			{#if kind === 'cpu'}
				<Cpu size={14} />
			{:else if kind === 'vram' || kind === 'memory'}
				<MemoryStick size={14} />
			{:else}
				<Gauge size={14} />
			{/if}
			{label}
		</span>
		<span class="gauge-live"><i></i> UPDATES</span>
	</div>

	<div class="gauge-face">
		<div class="steam-puff puff-a"></div>
		<div class="steam-puff puff-b"></div>
		<svg viewBox="0 0 200 125" role="img" aria-hidden="true">
			<path class="arc-track" d="M20 105 A80 80 0 0 1 180 105" />
			<path
				class="arc-value"
				d="M20 105 A80 80 0 0 1 180 105"
				style={`stroke-dasharray: ${percentage * (ARC_LENGTH / 100)} ${ARC_LENGTH}`}
			/>
			{#each Array(11) as _, index}
				<line
					class="gauge-tick"
					x1="100"
					y1="18"
					x2="100"
					y2="25"
					transform={`rotate(${-90 + index * 18} 100 105)`}
				/>
			{/each}
			<line
				class="gauge-needle"
				x1="100"
				y1="105"
				x2="100"
				y2="43"
				style={`transform: rotate(${needleAngle}deg); transform-origin: 100px 105px`}
			/>
			<circle class="gauge-hub" cx="100" cy="105" r="6" />
		</svg>
		<strong class="gauge-reading">{displayValue}</strong>
	</div>
	<div class="gauge-detail" title={detail}>{detail}</div>
</article>

<style>
	.steam-gauge {
		position: relative;
		min-width: 0;
		padding: 0.85rem 0.85rem 0.7rem;
		overflow: hidden;
		border: 1px solid var(--line);
		border-radius: 0.62rem;
		background:
			radial-gradient(
				circle at 50% 74%,
				color-mix(in srgb, var(--gauge-color) 13%, transparent),
				transparent 47%
			),
			var(--surface-raised);
		box-shadow: inset 0 1px rgba(255, 255, 255, 0.035);
	}
	.steam-gauge::after {
		content: '';
		position: absolute;
		inset: auto 10% 0;
		height: 1px;
		background: linear-gradient(90deg, transparent, var(--gauge-color), transparent);
		opacity: 0.75;
	}
	.steam-gauge.green {
		--gauge-color: var(--ok);
	}
	.steam-gauge.orange {
		--gauge-color: var(--accent);
	}
	.steam-gauge.purple {
		--gauge-color: var(--purple);
	}
	.gauge-head {
		display: flex;
		align-items: center;
		justify-content: space-between;
		gap: 0.5rem;
		font: 0.58rem var(--mono);
		letter-spacing: 0.08em;
	}
	.gauge-label {
		display: inline-flex;
		align-items: center;
		gap: 0.35rem;
		color: var(--fg);
	}
	.gauge-label :global(svg) {
		color: var(--gauge-color);
	}
	.gauge-live {
		display: inline-flex;
		align-items: center;
		gap: 0.25rem;
		color: var(--gauge-color);
		font-size: 0.5rem;
	}
	.gauge-live i {
		width: 0.32rem;
		height: 0.32rem;
		border-radius: 50%;
		background: var(--gauge-color);
		box-shadow: 0 0 8px var(--gauge-color);
		animation: gauge-pulse 1.5s ease-in-out infinite;
	}
	.gauge-face {
		position: relative;
		margin: 0.25rem -0.2rem -0.05rem;
	}
	.gauge-face svg {
		display: block;
		width: 100%;
		height: auto;
	}
	.arc-track,
	.arc-value {
		fill: none;
		stroke-width: 8;
		stroke-linecap: round;
	}
	.arc-track {
		stroke: var(--surface-soft);
	}
	.arc-value {
		stroke: var(--gauge-color);
		filter: drop-shadow(0 0 4px color-mix(in srgb, var(--gauge-color) 65%, transparent));
		transition: stroke-dasharray 650ms cubic-bezier(0.22, 1, 0.36, 1);
	}
	.gauge-tick {
		stroke: var(--line-strong);
		stroke-width: 2;
		opacity: 0.7;
	}
	.gauge-needle {
		stroke: var(--fg);
		stroke-width: 2.5;
		stroke-linecap: round;
		filter: drop-shadow(0 0 3px color-mix(in srgb, var(--fg) 55%, transparent));
		transition: transform 650ms cubic-bezier(0.22, 1, 0.36, 1);
	}
	.gauge-hub {
		fill: var(--gauge-color);
		stroke: var(--fg);
		stroke-width: 2;
	}
	.gauge-reading {
		position: absolute;
		left: 50%;
		bottom: 0.15rem;
		transform: translateX(-50%);
		color: var(--fg);
		font: 700 clamp(0.95rem, 2vw, 1.2rem) var(--mono);
		letter-spacing: -0.06em;
		white-space: nowrap;
	}
	.gauge-detail {
		overflow: hidden;
		color: var(--muted);
		font: 0.55rem var(--mono);
		letter-spacing: 0.01em;
		text-align: center;
		text-overflow: ellipsis;
		white-space: nowrap;
	}
	.steam-puff {
		position: absolute;
		z-index: 2;
		width: 0.5rem;
		height: 0.5rem;
		border: 1px solid color-mix(in srgb, var(--gauge-color) 70%, transparent);
		border-radius: 50%;
		opacity: 0;
		animation: steam-rise 3.4s ease-out infinite;
	}
	.puff-a {
		left: 27%;
		bottom: 34%;
	}
	.puff-b {
		left: 67%;
		bottom: 37%;
		animation-delay: 1.4s;
	}
	.unavailable {
		--gauge-color: var(--faint);
	}
	.unavailable .gauge-live {
		color: var(--faint);
	}
	.unavailable .gauge-live i {
		background: var(--faint);
		box-shadow: none;
		animation: none;
	}
	@keyframes gauge-pulse {
		50% {
			opacity: 0.35;
			transform: scale(0.72);
		}
	}
	@keyframes steam-rise {
		0% {
			opacity: 0;
			transform: translate(0, 0) scale(0.5);
		}
		20% {
			opacity: 0.65;
		}
		100% {
			opacity: 0;
			transform: translate(0.5rem, -1.35rem) scale(1.45);
		}
	}
	@media (prefers-reduced-motion: reduce) {
		.steam-puff,
		.gauge-live i {
			animation: none;
		}
	}
</style>
