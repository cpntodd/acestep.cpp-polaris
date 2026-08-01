<script lang="ts">
	import {
		Play,
		Square,
		Pencil,
		Download,
		Cpu,
		Trash2,
		ChevronDown,
		Heart,
		Type,
		TriangleAlert,
		Sparkles,
		Radio,
		ScanLine,
		Link2,
		Archive
	} from '@lucide/svelte';
	import { app, setRequest, toast } from '../lib/state.svelte.js';
	import { deleteSong } from '../lib/db.js';
	import { vaeEncode, pollJob, jobResultLatents } from '../lib/api.js';
	import { saveJob, clearJob, putSong } from '../lib/db.js';
	import type { Song } from '../lib/types.js';
	import { displaySongName } from '../lib/songName.js';
	import { analyzeReferenceSong } from '../lib/reference.js';
	import { exportReferenceTemplate, referenceTemplateFilename } from '../lib/template.js';
	import Waveform from './Waveform.svelte';
	import Menu, { type MenuItem } from './Menu.svelte';
	import Dialog from './Dialog.svelte';

	let { song }: { song: Song } = $props();

	let playing = $state(false);
	let time = $state(0);
	let dur = $state(0);
	let rangeStart = $state(0);
	let rangeEnd = $state(0);
	let editingStyle = $state(false);
	let styleDraft = $state('');

	let isRef = $derived(app.refSongId === song.id);
	let isSrc = $derived(song.id != null && app.srcSongIds.includes(song.id));
	let isReference = $derived(song.source === 'upload');
	let hasStyleProfile = $derived(!!song.stylePrompt || song.analysisState === 'ready');

	function languageLabel(code: string | undefined): string {
		if (!code) return '—';
		const labels: Record<string, string> = {
			mk: 'Macedonian',
			sr: 'Serbian',
			bg: 'Bulgarian'
		};
		return labels[code.toLowerCase()] ? `${labels[code.toLowerCase()]} (${code})` : code;
	}

	// "(variant task)" suffix rebuilt from the request, used for the card
	// title and download filenames. Song.name itself stays the base name.
	let displayName = $derived(displaySongName(song));

	function toggleSrc() {
		if (song.id == null) return;
		const next = isSrc
			? app.srcSongIds.filter((id) => id !== song.id)
			: [...app.srcSongIds, song.id];
		app.srcSongIds = next;
		if (next.length === 0) {
			app.srcRangeStart = null;
			app.srcRangeEnd = null;
			rangeStart = 0;
			rangeEnd = 0;
		} else {
			app.request.task_type = 'cover';
			toast(
				`${next.length} source ${next.length === 1 ? 'song' : 'songs'} combined — Cover mode enabled`,
				2800,
				true
			);
		}
	}

	function toggleTimbre() {
		if (isRef) {
			app.refSongId = null;
		} else {
			app.refSongId = song.id ?? null;
			toast('Timbre reference armed', 2400, true);
		}
	}

	// waveform drag to global state
	$effect(() => {
		if (isSrc && rangeEnd > rangeStart) {
			app.srcRangeStart = rangeStart;
			app.srcRangeEnd = rangeEnd;
		}
	});

	// global state to waveform visual (field input)
	$effect(() => {
		if (isSrc) {
			const rs = app.srcRangeStart;
			const re = app.srcRangeEnd;
			if (rs != null && re != null && re > rs) {
				rangeStart = rs;
				rangeEnd = re;
			} else {
				rangeStart = 0;
				rangeEnd = 0;
			}
		}
	});

	function toggle() {
		playing = !playing;
	}

	function load() {
		app.name = song.name;
		setRequest({ ...song.request });
		app.pendingRequests = [];
		app.pendingIndex = 0;
	}

	let scanning = $state(false);

	// analyze audio: send to /understand, fill form with detected metadata.
	// persists the job under 'lm' key so page reload resumes polling.
	// Uploads the cached latents when present so the server skips the VAE
	// encode. Understand is the canonical "complete this raw audio" op:
	// the source card is enriched in place with both outputs the server
	// produced for it, the latents that fed the FSQ tokenizer and the
	// detected metadata (caption, lyrics, codes, bpm, ...). Cover and synth
	// keep their "always create a new card" contract; only understand
	// touches an existing card and only the very one it analyzed.
	async function scan() {
		scanning = true;
		try {
			const { requests } = await analyzeReferenceSong(
				song,
				app.request.lm_model as string,
				app.request.synth_model as string
			);
			if (song.id != null) await putSong($state.snapshot(song));
			app.name = song.name;
			setRequest({ ...song.request });
			app.pendingRequests = requests;
			app.pendingIndex = 0;
		} catch (e: unknown) {
			song.analysisState = 'error';
			song.analysisError = e instanceof Error ? e.message : String(e);
			if (song.id != null) await putSong($state.snapshot(song)).catch(() => {});
			toast(e instanceof Error ? e.message : String(e));
		} finally {
			scanning = false;
		}
	}

	function applyStyle() {
		const style = song.stylePrompt || song.request.caption;
		if (!style) {
			toast('Analyze this reference first to create a style profile');
			return;
		}
		setRequest({ ...song.request, caption: style });
		app.name = `${song.name} · new take`;
		app.pendingRequests = [];
		app.pendingIndex = 0;
		toast('Style profile copied to Compose', 2800, true);
	}

	function editStyle() {
		styleDraft = song.stylePrompt || song.request.caption || '';
		editingStyle = true;
	}

	async function saveStyle() {
		const value = styleDraft.trim();
		if (!value) {
			toast('Add a style description before saving');
			return;
		}
		song.stylePrompt = value;
		song.caption = value;
		song.request.caption = value;
		song.analysisState = 'ready';
		song.analysisError = '';
		if (song.id != null) await putSong($state.snapshot(song));
		editingStyle = false;
		toast('Discovered style updated', 2800, true);
	}

	function downloadAudio() {
		const url = URL.createObjectURL(song.audio);
		const a = document.createElement('a');
		a.href = url;
		const safe = displayName.replace(/[\\/:*?"<>|\x00-\x1f]/g, '') || 'song';
		const ext = song.format.startsWith('wav') ? '.wav' : '.mp3';
		a.download = `${safe}${ext}`;
		a.click();
		URL.revokeObjectURL(url);
	}

	async function exportTemplate() {
		if (!isReference) return;
		try {
			const blob = await exportReferenceTemplate($state.snapshot(song));
			const url = URL.createObjectURL(blob);
			const a = document.createElement('a');
			a.href = url;
			a.download = referenceTemplateFilename(song);
			a.click();
			URL.revokeObjectURL(url);
			toast('Reference template exported', 2600, true);
		} catch (e: unknown) {
			toast(e instanceof Error ? e.message : String(e));
		}
	}

	// Download the cached latents blob as a .vae file. Symmetric to
	// downloadAudio: the .vae plays back via Open (POST /vae decode path)
	// or feeds a future synth/understand call as src_latents, skipping the
	// VAE encode on reuse.
	function downloadLatents() {
		if (!song.latents) return;
		const url = URL.createObjectURL(song.latents);
		const a = document.createElement('a');
		a.href = url;
		const safe = displayName.replace(/[\\/:*?"<>|\x00-\x1f]/g, '') || 'song';
		a.download = `${safe}.vae`;
		a.click();
		URL.revokeObjectURL(url);
	}

	// VAE-only scan: POST /vae with the source audio (encode path), attach
	// the fresh latents to the card non-destructively. Skips the LM
	// roundtrip. Ideal for priming a cover/repaint target without paying
	// the LM cost just to get the [VAE] badge lit.
	async function encodeOnly() {
		if (song.latents || song.id == null) return;
		scanning = true;
		try {
			const jobId = await vaeEncode(song.audio, app.request);
			saveJob('lm', jobId);
			await pollJob(jobId);
			const latents = await jobResultLatents(jobId);
			clearJob('lm');
			song.latents = latents;
			await putSong($state.snapshot(song));
		} catch (e: unknown) {
			toast(e instanceof Error ? e.message : String(e));
		} finally {
			scanning = false;
		}
	}

	let confirmDeleteOpen = $state(false);
	let confirmDeleteNonFavOpen = $state(false);
	let renameOpen = $state(false);
	let renameValue = $state('');

	async function toggleFavorite() {
		if (song.id == null) return;
		song.favorite = !song.favorite;
		await putSong($state.snapshot(song));
	}

	function openRename() {
		renameValue = song.name;
		renameOpen = true;
	}

	// Persists the new base name. The "(variant task)" suffix shown in the
	// header keeps deriving from request via displaySongName, so renaming
	// only touches the stored stem.
	async function doRename() {
		if (song.id == null) return;
		const v = renameValue.trim();
		if (!v || v === song.name) return;
		song.name = v;
		await putSong($state.snapshot(song));
	}

	async function doRemove() {
		if (song.id == null) return;
		if (app.refSongId === song.id) app.refSongId = null;
		app.srcSongIds = app.srcSongIds.filter((id) => id !== song.id);
		await deleteSong(song.id);
		const idx = app.songs.findIndex((s) => s.id === song.id);
		if (idx >= 0) app.songs.splice(idx, 1);
	}

	// Deletes every non-favorite track in the list, regardless of which
	// card the menu was opened from. The current card is included in the
	// purge if it is not flagged favorite.
	async function doRemoveNonFavorites() {
		const victims = app.songs.filter((s) => !s.favorite);
		for (const s of victims) {
			if (s.id == null) continue;
			if (app.refSongId === s.id) app.refSongId = null;
			app.srcSongIds = app.srcSongIds.filter((id) => id !== s.id);
			await deleteSong(s.id);
		}
		app.songs = app.songs.filter((s) => s.favorite);
	}

	// MM:SS:XX (hundredths) for current position
	function fmtPos(s: number): string {
		const m = Math.floor(s / 60);
		const sec = Math.floor(s % 60);
		const cs = Math.floor((s * 100) % 100);
		return (
			String(m).padStart(2, '0') +
			':' +
			String(sec).padStart(2, '0') +
			':' +
			String(cs).padStart(2, '0')
		);
	}

	// MM:SS for total duration
	function fmtDur(s: number): string {
		const m = Math.floor(s / 60);
		const sec = Math.floor(s % 60);
		return String(m).padStart(2, '0') + ':' + String(sec).padStart(2, '0');
	}

	// Single action menu: one entry per user intent. Order mirrors a natural
	// flow (tweak prompt -> rename -> grab audio -> work with latents -> inspect -> destroy).
	// Destructive entries open a confirm dialog.
	const actionItems: MenuItem[] = $derived([
		{
			icon: Pencil,
			label: 'Edit song details',
			hint: 'Load this song back into the form for editing',
			onSelect: load
		},
		{
			icon: Type,
			label: 'Rename song',
			hint: 'Change the name shown in your local library',
			onSelect: openRename
		},
		{
			icon: Download,
			label: 'Download audio',
			hint: 'Save this song as an MP3 or WAV file',
			onSelect: downloadAudio
		},
		{
			icon: Archive,
			label: 'Export reference template',
			hint: 'Save the audio, style description, settings, and cached analysis as one ZIP file',
			onSelect: exportTemplate,
			disabled: !isReference
		},
		{
			icon: Cpu,
			label: 'Prepare audio for reuse',
			hint: 'Save a local copy that makes future reference use faster',
			onSelect: encodeOnly,
			disabled: !!song.latents
		},
		{
			icon: Download,
			label: 'Download VAE latents',
			hint: 'Save the prepared audio data for later use',
			onSelect: downloadLatents,
			disabled: !song.latents
		},
		{
			icon: ScanLine,
			label: hasStyleProfile ? 'Refresh style profile' : 'Analyze style',
			hint: 'Read the track locally and find its style, lyrics, tempo, key, and other song details',
			onSelect: scan,
			disabled: scanning
		},
		{
			icon: Sparkles,
			label: 'Use this style',
			hint: 'Copy the discovered style description into the song form',
			onSelect: applyStyle,
			disabled: !hasStyleProfile
		},
		{
			icon: Radio,
			label: 'Use as source song',
			hint: 'Use this recording as the starting audio for a new version',
			onSelect: toggleSrc
		},
		{
			icon: Link2,
			label: 'Use its tone',
			hint: 'Use this recording to guide the voice and character of a new song',
			onSelect: toggleTimbre
		},
		{
			icon: Trash2,
			label: 'Delete this song',
			hint: 'Remove this song and its local audio from the library',
			onSelect: () => (confirmDeleteOpen = true)
		},
		{
			icon: TriangleAlert,
			label: 'Delete non-favorites',
			hint: 'Remove every library item that is not marked as a favorite',
			onSelect: () => (confirmDeleteNonFavOpen = true)
		}
	]);
</script>

<div class="card">
	<div class="card-header">
		<button class="icon-btn" onclick={toggle} title={playing ? 'Stop' : 'Play'}>
			{#if playing}
				<Square size={14} />
			{:else}
				<Play size={14} />
			{/if}
		</button>
		<div class="track-identity">
			<span class="card-name">{displayName}</span>
			<div class="track-badges">
				{#if isReference}<span class="track-badge reference">REFERENCE</span>{/if}
				{#if hasStyleProfile}<span class="track-badge analyzed">PROFILE READY</span>{/if}
			</div>
		</div>
		<Menu items={actionItems}>
			{#snippet trigger()}<ChevronDown size={14} /> Menu{/snippet}
		</Menu>
		<button
			class="icon-btn"
			onclick={toggleFavorite}
			title={song.favorite ? 'Unfavorite' : 'Favorite'}
		>
			<Heart size={14} fill={song.favorite ? 'currentColor' : 'none'} />
		</button>
	</div>
	<Waveform
		{song}
		bind:playing
		bind:time
		bind:dur
		selectable={isSrc && app.srcSongIds.length === 1}
		bind:rangeStart
		bind:rangeEnd
	/>
	{#if isReference || song.stylePrompt}
		<div class="style-profile" class:profile-error={song.analysisState === 'error'}>
			<div class="profile-header">
				<span><Sparkles size={13} /> DISCOVERED STYLE</span
				>{#if song.analysisState === 'analyzing'}<b class="profile-scanning">LISTENING…</b
					>{:else if song.analysisState === 'error'}<b>RETRY ANALYSIS</b
					>{:else if hasStyleProfile}<b>FOUND ON THIS COMPUTER</b>{/if}
			</div>
			{#if song.analysisState === 'error'}
				<p>{song.analysisError || 'Analyzer unavailable'}</p>
			{:else if hasStyleProfile}
				{#if editingStyle}
					<textarea
						class="style-editor"
						rows="4"
						bind:value={styleDraft}
						title="Edit the reusable style description"
					></textarea>
					<div class="style-editor-actions">
						<button class="small-action primary" type="button" onclick={saveStyle}
							>Save style</button
						>
						<button class="small-action" type="button" onclick={() => (editingStyle = false)}>
							Cancel
						</button>
					</div>
				{:else}
					<p>{song.stylePrompt || song.request.caption}</p>
					<button
						class="style-edit-button"
						type="button"
						onclick={editStyle}
						title="Edit this discovered style description"
					>
						<Pencil size={12} /> Edit style description
					</button>
				{/if}
				<div class="profile-metadata">
					<span><b>{song.request.bpm || '—'}</b> BPM</span>
					<span
						><b>{song.request.duration ? `${Math.round(song.request.duration)}s` : '—'}</b> LENGTH</span
					>
					<span><b>{song.request.keyscale || '—'}</b> KEY</span>
					<span><b>{song.request.timesignature || '—'}</b> BEAT</span>
					<span><b>{languageLabel(song.request.vocal_language)}</b> SINGING</span>
				</div>
			{:else}
				<p class="profile-empty">
					Run Analyze style to extract the prompt and settings from this track.
				</p>
			{/if}
		</div>
	{/if}
	<div class="card-footer">
		<span class="format-badge">{song.format.toUpperCase()}</span>
		{#if song.latents}
			<span class="format-badge">VAE</span>
		{/if}
		<span class="timecode">{fmtPos(time)} / {fmtDur(dur)}</span>
		<div class="card-actions">
			{#if isReference && !hasStyleProfile}
				<button class="small-action primary" type="button" onclick={scan} disabled={scanning}
					><ScanLine size={13} /> {scanning ? 'Listening…' : 'Analyze style'}</button
				>
			{:else if hasStyleProfile}
				<button class="small-action primary" type="button" onclick={applyStyle}
					><Sparkles size={13} /> Use style</button
				>
			{/if}
			<label class="source-toggle"
				><input
					type="checkbox"
					class="ref-check"
					checked={isSrc}
					onchange={toggleSrc}
					title="Source audio"
				/> Source</label
			>
			<label class="source-toggle"
				><input
					type="checkbox"
					class="ref-check"
					checked={isRef}
					onchange={toggleTimbre}
					title="Timbre reference"
				/> Timbre</label
			>
		</div>
	</div>
</div>

<Dialog bind:open={confirmDeleteOpen} title="Delete this track?" onConfirm={doRemove} />

<Dialog
	bind:open={confirmDeleteNonFavOpen}
	title="Delete non-favorites?"
	onConfirm={doRemoveNonFavorites}
/>

<Dialog bind:open={renameOpen} title="Rename song" onConfirm={doRename}>
	{#snippet body()}
		<input type="text" class="rename-input" bind:value={renameValue} />
	{/snippet}
</Dialog>

<style>
	.card {
		display: flex;
		flex-direction: column;
		gap: 0.55rem;
		padding: 0.85rem;
		border: 1px solid var(--line);
		border-radius: 0.65rem;
		background: var(--bg-card);
		box-shadow: 0 10px 25px rgba(0, 0, 0, 0.09);
		transition:
			border-color 180ms ease,
			transform 180ms ease,
			box-shadow 180ms ease;
	}
	.card:hover {
		border-color: color-mix(in srgb, var(--accent) 45%, var(--line));
		transform: translateY(-1px);
		box-shadow: 0 14px 32px rgba(0, 0, 0, 0.14);
	}
	.card-header {
		display: flex;
		align-items: center;
		gap: 0.4rem;
	}
	.track-identity {
		display: flex;
		flex-direction: column;
		gap: 0.25rem;
		min-width: 0;
		flex: 1;
	}
	.track-badges {
		display: flex;
		gap: 0.3rem;
	}
	.track-badge {
		font: 0.55rem var(--mono);
		letter-spacing: 0.05em;
		color: var(--muted);
	}
	.track-badge.reference {
		color: var(--accent);
	}
	.track-badge.analyzed {
		color: var(--ok);
	}
	.card-footer {
		display: flex;
		align-items: center;
		gap: 0.55rem;
		flex-wrap: wrap;
	}
	.card-name {
		font-size: 0.82rem;
		font-weight: 600;
		white-space: nowrap;
		overflow: hidden;
		text-overflow: ellipsis;
		flex: 1;
	}
	.format-badge {
		font: 0.55rem var(--mono);
		letter-spacing: 0.05em;
		padding: 0.05rem 0.3rem;
		border: 1px solid var(--line-strong);
		border-radius: 0.2rem;
		background: var(--surface-soft);
		color: var(--muted);
		flex-shrink: 0;
	}
	.timecode {
		font: 0.62rem var(--mono);
		color: var(--muted);
		white-space: nowrap;
		flex: 1;
	}
	.card-actions {
		display: flex;
		align-items: center;
		gap: 0.35rem;
		flex-shrink: 0;
		font-size: 0.67rem;
		margin-left: auto;
	}
	.icon-btn {
		background: none;
		border: none;
		cursor: pointer;
		padding: 0.15rem;
		color: var(--fg);
		display: flex;
		align-items: center;
		gap: 0.2rem;
		font-size: 0.8rem;
	}
	.icon-btn:hover {
		color: var(--focus);
	}
	.ref-check {
		cursor: pointer;
		accent-color: var(--focus);
	}
	.source-toggle {
		display: inline-flex;
		align-items: center;
		gap: 0.22rem;
		color: var(--muted);
		white-space: nowrap;
		cursor: pointer;
	}
	.source-toggle:hover {
		color: var(--accent);
	}
	.small-action {
		display: inline-flex;
		align-items: center;
		gap: 0.3rem;
		padding: 0.32rem 0.48rem;
		border: 1px solid var(--line);
		border-radius: 0.3rem;
		background: var(--surface-soft);
		color: var(--fg);
		font: 0.6rem var(--mono);
		cursor: pointer;
	}
	.small-action.primary {
		border-color: rgba(240, 144, 64, 0.5);
		background: rgba(240, 144, 64, 0.12);
		color: var(--accent);
	}
	.small-action:hover:not(:disabled) {
		border-color: var(--accent);
	}
	.small-action:disabled {
		opacity: 0.6;
		cursor: wait;
	}
	.style-profile {
		padding: 0.75rem;
		border: 1px solid rgba(240, 144, 64, 0.26);
		border-radius: 0.45rem;
		background: linear-gradient(135deg, rgba(240, 144, 64, 0.09), rgba(240, 144, 64, 0.025));
	}
	.style-profile.profile-error {
		border-color: rgba(214, 91, 82, 0.45);
		background: rgba(214, 91, 82, 0.08);
	}
	.profile-header {
		display: flex;
		align-items: center;
		justify-content: space-between;
		gap: 0.5rem;
		margin-bottom: 0.45rem;
		color: var(--accent);
		font: 0.58rem var(--mono);
		letter-spacing: 0.08em;
	}
	.profile-header span {
		display: inline-flex;
		align-items: center;
		gap: 0.3rem;
	}
	.profile-header b {
		color: var(--muted);
		font-weight: 400;
	}
	.profile-scanning {
		animation: profile-pulse 1s ease-in-out infinite;
	}
	.style-profile p {
		color: var(--fg);
		font-size: 0.78rem;
		line-height: 1.5;
	}
	.style-editor {
		width: 100%;
		min-height: 5.5rem;
		padding: 0.55rem;
		border: 1px solid var(--accent);
		border-radius: 0.35rem;
		background: var(--surface);
		color: var(--fg);
		font: 0.78rem/1.5 var(--sans);
		resize: vertical;
	}
	.style-editor-actions {
		display: flex;
		gap: 0.4rem;
		margin-top: 0.45rem;
	}
	.style-edit-button {
		display: inline-flex;
		align-items: center;
		gap: 0.3rem;
		margin-top: 0.45rem;
		padding: 0.25rem 0.4rem;
		border: 1px solid var(--line);
		border-radius: 0.25rem;
		background: transparent;
		color: var(--muted);
		font: 0.58rem var(--mono);
		cursor: pointer;
	}
	.style-edit-button:hover {
		border-color: var(--accent);
		color: var(--accent);
	}
	.style-profile .profile-empty {
		color: var(--muted);
	}
	.profile-metadata {
		display: flex;
		flex-wrap: wrap;
		gap: 0.65rem;
		margin-top: 0.65rem;
		color: var(--muted);
		font: 0.58rem var(--mono);
	}
	.profile-metadata b {
		color: var(--fg);
		font-weight: 400;
	}
	@keyframes profile-pulse {
		50% {
			opacity: 0.45;
		}
	}
	.rename-input {
		width: 100%;
		background: var(--bg-input);
		border: none;
		border-radius: 3px;
		padding: 0.25rem 0.4rem;
		color: var(--fg);
		font-size: 0.8rem;
	}
	.rename-input:focus {
		outline: 1px solid var(--focus);
	}
</style>
