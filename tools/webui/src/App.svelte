<script lang="ts">
	import {
		Activity,
		AudioLines,
		Database,
		Moon,
		PanelTop,
		Power,
		Sun,
		Terminal,
		Volume2,
		Zap
	} from '@lucide/svelte';
	import { app, toast } from './lib/state.svelte.js';
	import {
		metrics as fetchMetrics,
		props,
		serverControlStatus,
		startServer,
		stopServer
	} from './lib/api.js';
	import { getAllSongs } from './lib/db.js';
	import { METRICS_POLL_MS, PROPS_POLL_MS } from './lib/config.js';
	import RequestForm from './components/RequestForm.svelte';
	import ReferenceUploader from './components/ReferenceUploader.svelte';
	import SongList from './components/SongList.svelte';
	import SteamGauge from './components/SteamGauge.svelte';
	import Toast from './components/Toast.svelte';

	let uploadedCount = $derived(app.songs.filter((song) => song.source === 'upload').length);
	let analyzedCount = $derived(
		app.songs.filter((song) => song.source === 'upload' && song.analysisState === 'ready').length
	);
	let generatedCount = $derived(app.songs.filter((song) => song.source !== 'upload').length);
	let pipelineState = $derived(
		app.props?.models?.lm?.length && app.props?.models?.dit?.length
			? 'Ready to make music'
			: 'Waiting for the server'
	);
	let vramPercent = $derived(
		app.metrics?.vram.available && app.metrics.vram.total > 0
			? (app.metrics.vram.used / app.metrics.vram.total) * 100
			: null
	);
	type ServerState = 'on' | 'off' | 'starting' | 'unknown';
	let serverState = $state<ServerState>('unknown');
	let serverBusy = $state(false);
	let serverButtonText = $derived(
		serverState === 'on' ? 'OFF' : serverState === 'starting' ? 'WAIT' : 'ON'
	);
	let serverStateLabel = $derived(
		serverState === 'on'
			? 'Ready'
			: serverState === 'starting'
				? 'Starting'
				: serverState === 'off'
					? 'Stopped'
					: 'Checking server'
	);

	function formatBytes(bytes: number): string {
		if (!Number.isFinite(bytes) || bytes <= 0) return '—';
		const units = ['B', 'KB', 'MB', 'GB', 'TB'];
		let value = bytes;
		let index = 0;
		while (value >= 1024 && index < units.length - 1) {
			value /= 1024;
			index++;
		}
		return `${value.toFixed(index > 1 ? 1 : 0)} ${units[index]}`;
	}

	$effect(() => {
		getAllSongs()
			.then((songs) => (app.songs = songs.reverse()))
			.catch(() => {});
	});

	function pollProps() {
		props()
			.then((h) => (app.props = h))
			.catch(() => (app.props = null));
	}

	$effect(() => {
		pollProps();
		const id = setInterval(pollProps, PROPS_POLL_MS);
		return () => clearInterval(id);
	});

	function pollMetrics() {
		fetchMetrics()
			.then((value) => (app.metrics = value))
			.catch(() => (app.metrics = null));
	}

	$effect(() => {
		pollMetrics();
		const id = setInterval(pollMetrics, METRICS_POLL_MS);
		return () => clearInterval(id);
	});

	async function pollServerControl() {
		try {
			const status = await serverControlStatus();
			serverState = status.state;
		} catch {
			// Direct ace-server launches have no supervisor. A live /props reply
			// still tells us that the backend is on, and stopServer() has a
			// direct /shutdown fallback for that mode.
			serverState = app.props ? 'on' : 'unknown';
		}
	}

	$effect(() => {
		pollServerControl();
		const id = setInterval(pollServerControl, 1500);
		return () => clearInterval(id);
	});

	async function toggleServer() {
		if (serverBusy) return;
		const stopping = serverState === 'on' || serverState === 'starting';
		serverBusy = true;
		try {
			if (stopping) {
				await stopServer();
				serverState = 'off';
				toast('Server stopped. Press ON to bring the local engine back.', 5000, true);
			} else {
				await startServer();
				serverState = 'starting';
				toast('Starting the local model server…', 5000, true);
			}
		} catch (error) {
			toast(error instanceof Error ? error.message : 'Server control unavailable');
		}
		serverBusy = false;
	}

	function onVolume(e: Event) {
		app.volume = Number((e.target as HTMLInputElement).value);
	}

	$effect(() => {
		document.documentElement.classList.toggle('dark', app.dark);
		document.documentElement.classList.toggle('light', !app.dark);
	});
</script>

<svelte:head>
	<title>acestep.cpp · Local audio lab</title>
	<meta
		name="description"
		content="A local-first AI music workstation for turning reference tracks into style-aware compositions."
	/>
</svelte:head>

<div class="ace-app">
	<div class="ambient ambient-a"></div>
	<div class="ambient ambient-b"></div>
	<div class="grid-overlay"></div>

	<header class="topbar">
		<div class="brand-lockup">
			<div class="brand-mark"><Zap size={18} fill="currentColor" /></div>
			<div class="brand-copy">
				<strong>acestep<span>.cpp</span></strong>
				<small>PRIVATE MUSIC STUDIO</small>
			</div>
		</div>
		<div class="topbar-status">
			<span
				class:status-light-on={serverState === 'on'}
				class:status-light-off={serverState !== 'on'}
				class="status-light"
			></span>
			{serverStateLabel} <span class="divider">/</span> ONLY ON THIS COMPUTER
		</div>
		<div class="topbar-tools">
			<button
				class:on={serverState === 'on'}
				class:busy={serverBusy || serverState === 'starting'}
				class="power-switch"
				type="button"
				aria-pressed={serverState === 'on'}
				title={serverState === 'on'
					? 'Stop the local model server'
					: 'Start the local model server'}
				onclick={toggleServer}
			>
				<span class="power-switch-face">
					<span class="power-switch-label">MUSIC SERVER</span>
					<strong>{serverButtonText}</strong>
				</span>
				<Power size={14} strokeWidth={2.5} />
			</button>
			<div class="volume-control">
				<Volume2 size={14} />
				<input
					aria-label="Playback volume"
					type="range"
					min="0"
					max="1"
					step="0.01"
					value={app.volume}
					oninput={onVolume}
				/>
			</div>
			<button
				class="theme-button"
				type="button"
				onclick={() => (app.dark = !app.dark)}
				title="Toggle light and dark theme"
			>
				{#if app.dark}<Sun size={15} />{:else}<Moon size={15} />{/if}
			</button>
			<span class="build-stamp" title="The version of ACE-Step.cpp currently running"
				>Version {__ACE_VERSION__}</span
			>
		</div>
	</header>

	<main class="workspace">
		<section class="hero console-panel">
			<div class="window-chrome">
				<div class="traffic-lights"><i></i><i></i><i></i></div>
				<span><Terminal size={13} /> acestep@local: ~/studio</span>
				<code>session::reference</code>
			</div>
			<div class="hero-grid">
				<div class="hero-copy">
					<div class="eyebrow">
						<span class="prompt-symbol">›</span> MAKE MUSIC FROM A REFERENCE
					</div>
					<h1>Turn a track into your next <em>sound system.</em></h1>
					<p>
						Drop in a song. ACE-Step listens locally, extracts its musical DNA, and hands you a
						style prompt plus the controls to bend it into something new.
					</p>
					<div class="hero-command">
						<span>●</span>
						{uploadedCount
							? `${uploadedCount} reference ${uploadedCount === 1 ? 'track is' : 'tracks are'} ready`
							: 'Add a reference track to begin'}<span class="cursor"></span>
					</div>
				</div>
				<div class="telemetry-card">
					<div class="telemetry-head" title="A quick view of what the local music tools have ready">
						<span>WHAT IS READY</span><Activity size={15} />
					</div>
					<div class="telemetry-line">
						<span>STYLE READER</span><b>{app.props?.models?.lm?.length ? 'READY' : 'WAITING'}</b>
					</div>
					<div class="telemetry-line">
						<span>REFERENCE TRACKS</span><b>{uploadedCount}</b>
					</div>
					<div class="telemetry-line"><span>SAVED STYLE SETTINGS</span><b>{analyzedCount}</b></div>
					<div class="telemetry-meter">
						<span style={`width: ${uploadedCount ? Math.min(100, 18 + analyzedCount * 27) : 8}%`}
						></span>
					</div>
				</div>
			</div>
			<ReferenceUploader />
		</section>

		<section class="gauge-dock" aria-label="Live computer activity">
			<div class="dock-heading">
				<div>
					<span class="dock-kicker"><span class="signal-dot"></span> COMPUTER STATUS</span>
					<h2>Live activity</h2>
				</div>
				<span class="dock-note">{app.metrics ? 'UPDATES LIVE' : 'WAITING FOR SERVER'}</span>
			</div>
			<div class="steam-grid">
				<SteamGauge
					label="COMPUTER USE"
					kind="cpu"
					tone="green"
					value={app.metrics?.cpu.available ? app.metrics.cpu.usage : null}
					detail={app.metrics?.cpu.available
						? `${app.metrics.cpu.cores || '—'} cores available`
						: 'Computer use is unavailable'}
				/>
				<SteamGauge
					label="GRAPHICS USE"
					kind="gpu"
					tone="orange"
					value={app.metrics?.gpu.usage_available ? app.metrics.gpu.usage : null}
					detail={app.metrics?.gpu.usage_available
						? app.metrics.gpu.name || app.metrics.gpu.backend || 'Graphics card is active'
						: app.metrics?.gpu.available
							? 'Graphics use is not reported'
							: 'No graphics card detected'}
				/>
				<SteamGauge
					label="GRAPHICS MEMORY"
					kind="vram"
					tone="purple"
					value={vramPercent}
					detail={app.metrics?.vram.available
						? `${formatBytes(app.metrics.vram.used)} / ${formatBytes(app.metrics.vram.total)}`
						: 'Graphics memory is unavailable'}
				/>
			</div>
		</section>

		<section class="metrics" aria-label="Library status">
			<div class="metric-card">
				<div class="metric-icon orange"><AudioLines size={17} /></div>
				<div><span>REFERENCE TRACKS</span><strong>{uploadedCount}</strong></div>
				<small>ON THIS COMPUTER</small>
			</div>
			<div class="metric-card">
				<div class="metric-icon green"><Zap size={17} /></div>
				<div><span>SAVED STYLE PROFILES</span><strong>{analyzedCount}</strong></div>
				<small>READY TO REUSE</small>
			</div>
			<div class="metric-card">
				<div class="metric-icon blue"><Database size={17} /></div>
				<div><span>CREATED SONGS</span><strong>{generatedCount}</strong></div>
				<small>IN YOUR LIBRARY</small>
			</div>
			<div class="metric-card runtime-card">
				<div class="metric-icon purple"><PanelTop size={17} /></div>
				<div><span>MUSIC SERVER</span><strong>{serverStateLabel}</strong></div>
				<small>{app.props?.version ? `Version ${app.props.version}` : 'Not running'}</small>
			</div>
		</section>

		<div class="workspace-grid">
			<section class="panel form-panel">
				<div class="panel-header">
					<div>
						<span class="panel-index">01</span>
						<div>
							<h2>Make a song</h2>
							<p>Tell us what you want to hear</p>
						</div>
					</div>
					<span class="panel-tag">DESCRIBE YOUR SONG</span>
				</div>
				<RequestForm />
			</section>
			<section class="panel songs-panel">
				<div class="panel-header">
					<div>
						<span class="panel-index">02</span>
						<div>
							<h2>Your music</h2>
							<p>References, styles, and finished songs</p>
						</div>
					</div>
					<span class="panel-tag">{app.songs.length} ITEMS</span>
				</div>
				<SongList />
			</section>
		</div>
	</main>
</div>

<Toast />

<style>
	:global(:root) {
		--bg: #151515;
		--surface: #1e1e1f;
		--surface-raised: #252526;
		--surface-soft: #2b2b2d;
		--line: #363638;
		--line-strong: #505054;
		--fg: #f3f0ec;
		--muted: #8f8c89;
		--faint: #5f5d5a;
		--accent: #f09040;
		--accent-hot: #f5a623;
		--ok: #28c840;
		--blue: #55a5d9;
		--purple: #a478d1;
		--error: #d65b52;
		--bg-input: #19191a;
		--bg-card: #232324;
		--bg-btn: #303033;
		--bg-btn-hover: #3b3b3f;
		--focus: var(--accent);
		--waveform-dim: #56565a;
		--waveform-play: var(--accent);
		--waveform-range: #df6c58;
		--mono: 'Space Mono', 'SFMono-Regular', Consolas, 'Liberation Mono', monospace;
		color-scheme: dark;
	}
	:global(:root.light) {
		--bg: #e9e5df;
		--surface: #f7f4ef;
		--surface-raised: #fffdf9;
		--surface-soft: #ebe7e1;
		--line: #d5cec4;
		--line-strong: #b9afa2;
		--fg: #242322;
		--muted: #706b65;
		--faint: #9b958e;
		--bg-input: #fffdf9;
		--bg-card: #fffdf9;
		--bg-btn: #e2ddd6;
		--bg-btn-hover: #d7d0c8;
		--waveform-dim: #b3aca3;
		color-scheme: light;
	}
	:global(*, *::before, *::after) {
		box-sizing: border-box;
		margin: 0;
	}
	:global(body) {
		font-family:
			Inter,
			ui-sans-serif,
			system-ui,
			-apple-system,
			BlinkMacSystemFont,
			'Segoe UI',
			sans-serif;
		background: var(--bg);
		color: var(--fg);
		min-height: 100dvh;
	}
	:global(button),
	:global(input),
	:global(textarea),
	:global(select) {
		font: inherit;
	}
	:global(button:focus-visible),
	:global(input:focus-visible),
	:global(textarea:focus-visible),
	:global(select:focus-visible) {
		outline: 2px solid var(--accent);
		outline-offset: 2px;
	}
	.ace-app {
		position: relative;
		min-height: 100dvh;
		overflow: hidden;
		background: var(--bg);
	}
	.ambient {
		position: fixed;
		z-index: 0;
		width: 32rem;
		height: 32rem;
		border-radius: 50%;
		filter: blur(90px);
		pointer-events: none;
		opacity: 0.12;
	}
	.ambient-a {
		top: -14rem;
		right: -7rem;
		background: var(--accent);
		animation: float-a 15s ease-in-out infinite;
	}
	.ambient-b {
		bottom: -18rem;
		left: -10rem;
		background: #6d4c9c;
		animation: float-b 19s ease-in-out infinite;
	}
	.grid-overlay {
		position: fixed;
		inset: 0;
		z-index: 0;
		pointer-events: none;
		opacity: 0.18;
		background-image:
			linear-gradient(var(--line) 1px, transparent 1px),
			linear-gradient(90deg, var(--line) 1px, transparent 1px);
		background-size: 46px 46px;
		mask-image: linear-gradient(to bottom, black, transparent 85%);
	}
	.topbar,
	.workspace {
		position: relative;
		z-index: 1;
	}
	.topbar {
		display: flex;
		align-items: center;
		gap: 1.5rem;
		max-width: 1480px;
		margin: 0 auto;
		padding: 1.15rem 2rem;
		border-bottom: 1px solid color-mix(in srgb, var(--line) 70%, transparent);
	}
	.brand-lockup {
		display: flex;
		align-items: center;
		gap: 0.7rem;
	}
	.brand-mark {
		display: grid;
		place-items: center;
		width: 2.25rem;
		height: 2.25rem;
		border: 1px solid rgba(240, 144, 64, 0.55);
		border-radius: 0.55rem;
		color: var(--accent);
		background: rgba(240, 144, 64, 0.12);
		box-shadow: 0 0 25px rgba(240, 144, 64, 0.12);
	}
	.brand-copy {
		display: flex;
		flex-direction: column;
		gap: 0.08rem;
	}
	.brand-copy strong {
		font:
			1.15rem 'Courier New',
			var(--mono);
		letter-spacing: 0.02em;
	}
	.brand-copy strong span {
		color: var(--accent);
	}
	.brand-copy small,
	.topbar-status,
	.build-stamp {
		font: 0.61rem var(--mono);
		letter-spacing: 0.1em;
		color: var(--muted);
	}
	.topbar-status {
		display: flex;
		align-items: center;
		gap: 0.45rem;
		margin-left: 1.2rem;
	}
	.status-light {
		width: 0.42rem;
		height: 0.42rem;
		border-radius: 50%;
	}
	.status-light-on {
		background: var(--ok);
		box-shadow: 0 0 9px var(--ok);
		animation: pulse-dot 1.8s ease-in-out infinite;
	}
	.status-light-off {
		background: var(--accent-hot);
		box-shadow: 0 0 8px var(--accent-hot);
	}
	.divider {
		color: var(--line-strong);
	}
	.topbar-tools {
		display: flex;
		align-items: center;
		gap: 0.85rem;
		margin-left: auto;
	}
	.volume-control {
		display: flex;
		align-items: center;
		gap: 0.45rem;
		color: var(--muted);
	}
	.volume-control input {
		width: 70px;
		accent-color: var(--accent);
		cursor: pointer;
	}
	.theme-button {
		display: grid;
		place-items: center;
		width: 2rem;
		height: 2rem;
		padding: 0;
		border: 1px solid var(--line);
		border-radius: 0.4rem;
		background: var(--surface);
		color: var(--muted);
		cursor: pointer;
	}
	.theme-button:hover {
		color: var(--accent);
		border-color: var(--accent);
	}
	.power-switch {
		display: inline-flex;
		align-items: center;
		gap: 0.45rem;
		min-width: 5.2rem;
		height: 2.25rem;
		padding: 0.22rem 0.42rem 0.22rem 0.55rem;
		border: 1px solid #684d37;
		border-radius: 0.28rem;
		background: linear-gradient(180deg, rgba(255, 184, 102, 0.1), transparent 45%), #1b1716;
		box-shadow:
			inset 0 0 0 1px rgba(255, 214, 150, 0.05),
			0 0 0 1px rgba(0, 0, 0, 0.3),
			0 0 14px rgba(240, 144, 64, 0.08);
		color: var(--accent);
		cursor: pointer;
		font-family: var(--mono);
		transition:
			border-color 160ms ease,
			box-shadow 160ms ease,
			transform 160ms ease;
	}
	.power-switch:hover {
		border-color: var(--accent);
		box-shadow:
			inset 0 0 0 1px rgba(255, 214, 150, 0.08),
			0 0 16px rgba(240, 144, 64, 0.22);
		transform: translateY(-1px);
	}
	.power-switch:active {
		transform: translateY(1px);
	}
	.power-switch-face {
		display: flex;
		flex-direction: column;
		align-items: flex-start;
		gap: 0.05rem;
		line-height: 1;
	}
	.power-switch-label {
		color: #a88b71;
		font-size: 0.43rem;
		letter-spacing: 0.12em;
	}
	.power-switch strong {
		font-size: 0.68rem;
		letter-spacing: 0.08em;
		text-shadow: 0 0 8px currentColor;
	}
	.power-switch.on {
		border-color: rgba(56, 224, 112, 0.72);
		color: #54ef8a;
		box-shadow:
			inset 0 0 0 1px rgba(120, 255, 160, 0.08),
			0 0 16px rgba(56, 224, 112, 0.18);
	}
	.power-switch.busy {
		cursor: wait;
		animation: power-flicker 720ms steps(2, jump-none) infinite;
	}
	.build-stamp {
		max-width: 14rem;
		overflow: hidden;
		text-overflow: ellipsis;
		white-space: nowrap;
	}
	.workspace {
		display: flex;
		flex-direction: column;
		gap: 1rem;
		max-width: 1480px;
		margin: 0 auto;
		padding: 1.7rem 2rem 3rem;
	}
	.console-panel,
	.panel {
		border: 1px solid var(--line);
		border-radius: 0.75rem;
		background: color-mix(in srgb, var(--surface) 95%, transparent);
		box-shadow: 0 18px 60px rgba(0, 0, 0, 0.13);
	}
	.hero {
		overflow: hidden;
	}
	.window-chrome {
		display: flex;
		align-items: center;
		gap: 0.7rem;
		min-height: 2.5rem;
		padding: 0 1rem;
		border-bottom: 1px solid var(--line);
		background: var(--surface-raised);
		color: var(--muted);
		font: 0.65rem var(--mono);
	}
	.window-chrome span {
		display: inline-flex;
		align-items: center;
		gap: 0.4rem;
	}
	.window-chrome code {
		margin-left: auto;
		color: var(--faint);
	}
	.traffic-lights {
		display: flex;
		gap: 0.35rem;
	}
	.traffic-lights i {
		width: 0.55rem;
		height: 0.55rem;
		border-radius: 50%;
		background: #ff5f57;
	}
	.traffic-lights i:nth-child(2) {
		background: #febc2e;
	}
	.traffic-lights i:nth-child(3) {
		background: #28c840;
	}
	.hero-grid {
		display: grid;
		grid-template-columns: minmax(0, 1fr) minmax(18rem, 26rem);
		gap: 2rem;
		padding: 2rem 2.1rem 1.6rem;
	}
	.hero-copy {
		max-width: 48rem;
	}
	.eyebrow,
	.panel-tag,
	.metric-card span,
	.telemetry-card,
	.hero-command {
		font: 0.65rem var(--mono);
		letter-spacing: 0.08em;
	}
	.eyebrow {
		color: var(--accent);
	}
	.prompt-symbol {
		display: inline-block;
		margin-right: 0.35rem;
		font-size: 1.2rem;
		line-height: 0;
	}
	h1 {
		max-width: 42rem;
		margin: 0.75rem 0;
		font-size: clamp(2.2rem, 5vw, 4.5rem);
		line-height: 0.98;
		letter-spacing: -0.065em;
		font-weight: 700;
	}
	h1 em {
		color: var(--accent);
		font-style: normal;
		text-shadow: 0 0 30px rgba(240, 144, 64, 0.18);
	}
	.hero-copy > p {
		max-width: 42rem;
		color: var(--muted);
		font-size: 0.92rem;
		line-height: 1.65;
	}
	.hero-command {
		display: inline-flex;
		align-items: center;
		gap: 0.45rem;
		margin-top: 1.4rem;
		color: var(--muted);
		letter-spacing: 0.02em;
	}
	.hero-command > span:first-child {
		color: var(--ok);
	}
	.cursor {
		display: inline-block;
		width: 0.45rem;
		height: 0.9rem;
		background: var(--accent);
		box-shadow: 0 0 10px var(--accent);
		animation: blink 1s steps(2, jump-none) infinite;
	}
	.telemetry-card {
		align-self: stretch;
		padding: 1rem;
		border: 1px solid var(--line);
		border-radius: 0.55rem;
		background: rgba(0, 0, 0, 0.12);
		color: var(--muted);
	}
	.telemetry-head {
		display: flex;
		justify-content: space-between;
		align-items: center;
		padding-bottom: 0.7rem;
		border-bottom: 1px solid var(--line);
		color: var(--accent);
	}
	.telemetry-line {
		display: flex;
		justify-content: space-between;
		gap: 1rem;
		padding: 0.66rem 0;
		border-bottom: 1px solid color-mix(in srgb, var(--line) 55%, transparent);
		font-size: 0.6rem;
	}
	.telemetry-line b {
		color: var(--fg);
		font-weight: 400;
		text-align: right;
	}
	.telemetry-meter {
		height: 0.3rem;
		margin-top: 0.75rem;
		overflow: hidden;
		border-radius: 1rem;
		background: var(--surface-soft);
	}
	.telemetry-meter span {
		display: block;
		height: 100%;
		border-radius: inherit;
		background: linear-gradient(90deg, var(--accent), var(--accent-hot));
		box-shadow: 0 0 14px var(--accent);
		transition: width 500ms ease;
	}
	.hero :global(.reference-uploader) {
		margin: 0 2.1rem 1.35rem;
	}
	.metrics {
		display: grid;
		grid-template-columns: repeat(4, 1fr);
		gap: 1rem;
	}
	.gauge-dock {
		padding: 1rem 1.15rem 1.15rem;
		border: 1px solid var(--line);
		border-radius: 0.75rem;
		background:
			linear-gradient(115deg, color-mix(in srgb, var(--accent) 7%, transparent), transparent 42%),
			var(--surface);
		box-shadow: 0 18px 60px rgba(0, 0, 0, 0.1);
	}
	.dock-heading {
		display: flex;
		align-items: end;
		justify-content: space-between;
		gap: 1rem;
		margin-bottom: 0.8rem;
	}
	.dock-kicker {
		display: inline-flex;
		align-items: center;
		gap: 0.35rem;
		color: var(--accent);
		font: 0.58rem var(--mono);
		letter-spacing: 0.1em;
	}
	.dock-heading h2 {
		margin-top: 0.25rem;
		font-size: 1.05rem;
		letter-spacing: -0.03em;
	}
	.dock-note {
		color: var(--faint);
		font: 0.57rem var(--mono);
		letter-spacing: 0.06em;
	}
	.steam-grid {
		display: grid;
		grid-template-columns: repeat(3, minmax(0, 1fr));
		gap: 0.75rem;
	}
	.metric-card {
		display: flex;
		align-items: center;
		gap: 0.7rem;
		min-width: 0;
		padding: 0.9rem 1rem;
		border: 1px solid var(--line);
		border-radius: 0.6rem;
		background: var(--surface);
	}
	.metric-icon {
		display: grid;
		place-items: center;
		flex: 0 0 auto;
		width: 2rem;
		height: 2rem;
		border-radius: 0.45rem;
	}
	.metric-icon.orange {
		color: var(--accent);
		background: rgba(240, 144, 64, 0.12);
	}
	.metric-icon.green {
		color: var(--ok);
		background: rgba(40, 200, 64, 0.1);
	}
	.metric-icon.blue {
		color: var(--blue);
		background: rgba(85, 165, 217, 0.1);
	}
	.metric-icon.purple {
		color: var(--purple);
		background: rgba(164, 120, 209, 0.1);
	}
	.metric-card div:nth-child(2) {
		display: flex;
		flex-direction: column;
		gap: 0.2rem;
		min-width: 0;
	}
	.metric-card span {
		color: var(--muted);
		font-size: 0.58rem;
		white-space: nowrap;
	}
	.metric-card strong {
		font-size: 1rem;
		letter-spacing: -0.02em;
	}
	.metric-card small {
		margin-left: auto;
		color: var(--faint);
		font: 0.58rem var(--mono);
		white-space: nowrap;
	}
	.runtime-card strong {
		font: 0.85rem var(--mono);
		color: var(--ok);
	}
	.workspace-grid {
		display: grid;
		grid-template-columns: minmax(22rem, 0.72fr) minmax(0, 1.28fr);
		gap: 1rem;
		align-items: start;
	}
	.panel {
		min-width: 0;
		padding: 1.15rem;
	}
	.panel-header {
		display: flex;
		align-items: center;
		justify-content: space-between;
		gap: 1rem;
		margin-bottom: 1.15rem;
		padding-bottom: 0.9rem;
		border-bottom: 1px solid var(--line);
	}
	.panel-header > div {
		display: flex;
		align-items: center;
		gap: 0.7rem;
	}
	.panel-index {
		color: var(--accent);
		font: 0.68rem var(--mono);
	}
	.panel-header h2 {
		font-size: 1rem;
		letter-spacing: -0.02em;
	}
	.panel-header p {
		margin-top: 0.15rem;
		color: var(--muted);
		font: 0.64rem var(--mono);
	}
	.panel-tag {
		color: var(--faint);
		font-size: 0.58rem;
		white-space: nowrap;
	}
	:global(.request-form) {
		gap: 0.85rem !important;
	}
	:global(.request-form .toolbar) {
		padding-bottom: 0.2rem;
	}
	:global(.request-form .toolbar button),
	:global(.request-form .action-row button),
	:global(.request-form .lm-row button) {
		border: 1px solid var(--line);
		border-radius: 0.45rem;
		background: var(--bg-btn);
		color: var(--fg);
		font: 0.68rem var(--mono);
		transition:
			border-color 160ms ease,
			background 160ms ease,
			transform 160ms ease;
	}
	:global(.request-form .toolbar button:hover),
	:global(.request-form .action-row button:hover),
	:global(.request-form .lm-row button:hover) {
		border-color: var(--accent);
		background: var(--bg-btn-hover);
		transform: translateY(-1px);
	}
	:global(.request-form .action-row button:first-child) {
		border-color: rgba(240, 144, 64, 0.6);
		background: rgba(240, 144, 64, 0.15);
		color: var(--accent);
	}
	:global(.request-form textarea),
	:global(.request-form input[type='text']),
	:global(.request-form input[type='number']),
	:global(.request-form select) {
		border-color: var(--line);
		border-radius: 0.4rem;
		background: var(--bg-input);
		color: var(--fg);
	}
	:global(.request-form textarea:focus),
	:global(.request-form input:focus),
	:global(.request-form select:focus) {
		outline: 1px solid var(--accent);
		box-shadow: 0 0 18px rgba(240, 144, 64, 0.08);
	}
	:global(.request-form .section-title) {
		color: var(--fg);
		font: 0.64rem var(--mono);
		letter-spacing: 0.08em;
		text-transform: uppercase;
	}
	:global(.request-form details) {
		border-top: 1px solid var(--line);
	}
	:global(.request-form details summary) {
		color: var(--muted);
		font: 0.65rem var(--mono);
		letter-spacing: 0.05em;
		text-transform: uppercase;
	}
	:global(.request-form details[open] summary) {
		color: var(--accent);
	}
	:global(.request-form .model-label),
	:global(.request-form label) {
		color: var(--muted);
		font-size: 0.72rem;
	}
	:global(.request-form .dit-ind) {
		border: 1px solid var(--line);
		border-radius: 0.3rem;
		background: var(--surface-soft);
		color: var(--muted);
	}
	:global(.request-form .dit-ind.on) {
		border-color: rgba(40, 200, 64, 0.55);
		background: rgba(40, 200, 64, 0.1);
		color: var(--ok);
	}
	:global(.request-form .track-pill.active) {
		border-color: var(--accent);
		background: rgba(240, 144, 64, 0.12);
		color: var(--accent);
	}
	@keyframes blink {
		50% {
			opacity: 0;
		}
	}
	@keyframes pulse-dot {
		0%,
		100% {
			opacity: 0.55;
			transform: scale(0.9);
		}
		50% {
			opacity: 1;
			transform: scale(1.12);
		}
	}
	@keyframes power-flicker {
		0%,
		100% {
			opacity: 1;
		}
		50% {
			opacity: 0.58;
		}
	}
	@keyframes float-a {
		50% {
			transform: translate(-2rem, 2rem) scale(1.08);
		}
	}
	@keyframes float-b {
		50% {
			transform: translate(2rem, -1rem) scale(1.08);
		}
	}
	@media (max-width: 1050px) {
		.hero-grid {
			grid-template-columns: 1fr;
		}
		.telemetry-card {
			max-width: none;
		}
		.workspace-grid {
			grid-template-columns: 1fr;
		}
		.form-panel {
			order: 1;
		}
		.songs-panel {
			order: 2;
		}
	}
	@media (max-width: 760px) {
		.topbar,
		.workspace {
			padding-left: 1rem;
			padding-right: 1rem;
		}
		.topbar-status,
		.build-stamp,
		.volume-control {
			display: none;
		}
		.hero-grid {
			padding: 1.45rem 1.15rem 1.15rem;
		}
		.hero :global(.reference-uploader) {
			margin: 0 1.15rem 1.15rem;
		}
		.metrics {
			grid-template-columns: repeat(2, 1fr);
		}
		.steam-grid {
			grid-template-columns: repeat(3, minmax(8rem, 1fr));
			overflow-x: auto;
		}
		.steam-grid :global(.steam-gauge) {
			min-width: 9.5rem;
		}
		.metric-card small {
			display: none;
		}
	}
	@media (max-width: 480px) {
		h1 {
			font-size: 2.35rem;
		}
		.metrics {
			gap: 0.55rem;
		}
		.metric-card {
			padding: 0.7rem;
		}
		.gauge-dock {
			padding: 0.8rem;
		}
		.dock-heading {
			align-items: start;
			flex-direction: column;
			gap: 0.4rem;
		}
		.panel {
			padding: 0.85rem;
		}
	}
	@media (prefers-reduced-motion: reduce) {
		:global(*),
		:global(*::before),
		:global(*::after) {
			scroll-behavior: auto !important;
			animation-duration: 0.001ms !important;
			animation-iteration-count: 1 !important;
			transition-duration: 0.001ms !important;
		}
	}
</style>
