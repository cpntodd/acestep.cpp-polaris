<script lang="ts">
	import { Archive, AudioLines, LockKeyhole, ScanLine, Upload, WandSparkles } from '@lucide/svelte';
	import { app, setRequest, toast } from '../lib/state.svelte.js';
	import { putSong } from '../lib/db.js';
	import { cancelJob } from '../lib/api.js';
	import { analyzeReferenceSong, isReferenceAudio, storeReferenceTrack } from '../lib/reference.js';
	import { importReferenceTemplate, isReferenceTemplate } from '../lib/template.js';
	import type { Song } from '../lib/types.js';

	let input: HTMLInputElement;
	let templateInput: HTMLInputElement;
	let dragging = $state(false);
	let importing = $state(false);
	let analyzingBatch = $state(false);
	let batchStopRequested = $state(false);
	let activeBatchJobId = $state<string | null>(null);
	let referenceCount = $derived(app.songs.filter((song) => song.source === 'upload').length);

	async function importFile(file: File | undefined) {
		if (!file) return;
		if (isReferenceTemplate(file)) {
			importing = true;
			try {
				const song = await importReferenceTemplate(file);
				app.songs.unshift(song);
				app.name = song.name;
				app.pendingRequests = [];
				app.pendingIndex = 0;
				setRequest({ ...song.request, task_type: 'cover' });
				app.srcSongIds = song.id != null ? [song.id] : [];
				toast(`${song.name} template imported and armed`, 4200, true);
			} catch (error: unknown) {
				toast(error instanceof Error ? error.message : String(error));
			} finally {
				importing = false;
				if (templateInput) templateInput.value = '';
			}
			return;
		}
		if (!isReferenceAudio(file)) {
			toast('Reference tracks must be MP3/WAV files or ACE-Step template ZIPs');
			return;
		}
		importing = true;
		try {
			const song = await storeReferenceTrack(file);
			app.songs.unshift(song);
			app.name = song.name;
			app.srcSongIds = song.id != null ? [song.id] : [];
			app.request.task_type = 'cover';
			toast(`${song.name} is ready to analyze`, 4200, true);
		} catch (error: unknown) {
			toast(error instanceof Error ? error.message : String(error));
		} finally {
			importing = false;
			if (input) input.value = '';
		}
	}

	async function importFiles(files: FileList | File[] | null | undefined) {
		const selected = Array.from(files ?? []);
		if (selected.length === 0) return;
		if (selected.length === 1) {
			await importFile(selected[0]);
			return;
		}
		if (selected.some(isReferenceTemplate)) {
			toast('Import template ZIPs one at a time. MP3/WAV files can be added together.');
			return;
		}

		importing = true;
		const added: Song[] = [];
		let failed = 0;
		try {
			for (const file of selected) {
				if (!isReferenceAudio(file)) {
					failed++;
					continue;
				}
				try {
					added.push(await storeReferenceTrack(file));
				} catch {
					failed++;
				}
			}
			for (const song of added) app.songs.unshift(song);
			if (added.length > 0) {
				app.name = added[0].name;
				app.srcSongIds = added.map((song) => song.id).filter((id): id is number => id != null);
				app.request.task_type = 'cover';
			}
			toast(
				`${added.length} reference ${added.length === 1 ? 'song' : 'songs'} saved${
					failed ? `, ${failed} skipped` : ''
				}. They are selected together as source material.`,
				5000,
				failed === 0
			);
		} finally {
			importing = false;
			if (input) input.value = '';
		}
	}

	function onDrop(event: DragEvent) {
		event.preventDefault();
		dragging = false;
		void importFiles(event.dataTransfer?.files);
	}

	function stopBatch() {
		batchStopRequested = true;
		if (activeBatchJobId) void cancelJob(activeBatchJobId).catch(() => {});
	}

	async function analyzeBatch() {
		// Re-read every reference, including profiles made before the
		// Macedonian-aware listener was updated. Existing local songs can refresh
		// their language and style DNA without being uploaded again.
		const targets = app.songs.filter(
			(song) => song.source === 'upload' && song.analysisState !== 'analyzing'
		);
		if (targets.length === 0) {
			toast('No saved reference songs are available to refresh.', 3200, true);
			return;
		}
		analyzingBatch = true;
		batchStopRequested = false;
		let completed = 0;
		let failed = 0;
		try {
			for (const song of targets) {
				if (batchStopRequested) break;
				try {
					await analyzeReferenceSong(
						song,
						app.request.lm_model as string,
						app.request.synth_model as string,
						{
							onJobId: (id) => (activeBatchJobId = id),
							isCancelled: () => batchStopRequested
						}
					);
					if (song.id != null) await putSong($state.snapshot(song));
					completed++;
				} catch {
					if (batchStopRequested) break;
					failed++;
				}
				activeBatchJobId = null;
			}
			toast(
				batchStopRequested
					? `Stopped after refreshing ${completed} reference ${completed === 1 ? 'song' : 'songs'}.`
					: `Finished: ${completed} analyzed${failed ? `, ${failed} could not be read` : ''}.`,
				5000,
				!batchStopRequested && failed === 0
			);
		} finally {
			activeBatchJobId = null;
			analyzingBatch = false;
		}
	}
</script>

<div
	class="reference-uploader"
	class:dragging
	role="region"
	aria-label="Local reference track drop zone"
	ondragover={(event) => {
		event.preventDefault();
		dragging = true;
	}}
	ondragleave={() => (dragging = false)}
	ondrop={onDrop}
>
	<input
		bind:this={input}
		type="file"
		accept=".mp3,.wav,audio/mpeg,audio/wav"
		multiple
		hidden
		onchange={(event) => void importFiles(event.currentTarget.files)}
	/>
	<input
		bind:this={templateInput}
		type="file"
		accept=".zip,application/zip"
		hidden
		onchange={(event) => void importFile(event.currentTarget.files?.[0])}
	/>
	<div class="upload-icon"><AudioLines size={24} /></div>
	<div class="upload-copy">
		<div class="upload-kicker"><span class="signal-dot"></span> START WITH A SONG</div>
		<strong>Drop in a song to discover its style</strong>
		<span>We keep the audio here and find a reusable description and settings.</span>
	</div>
	<div class="upload-actions">
		<button class="upload-button" type="button" onclick={() => input.click()} disabled={importing}>
			{#if importing}<WandSparkles size={15} /> Saving{:else}<Upload size={15} /> Add songs{/if}
		</button>
		<button
			class="template-button"
			type="button"
			onclick={() => templateInput.click()}
			disabled={importing}
			title="Bring back a saved song, style description, settings, and audio from a template ZIP"
		>
			<Archive size={14} /> Import template
		</button>
	</div>
	{#if referenceCount > 0}
		<button
			class="batch-analyze"
			type="button"
			disabled={importing}
			onclick={analyzingBatch ? stopBatch : analyzeBatch}
			title="Read every saved reference song again, one at a time, and refresh its style, language, lyrics, tempo, key, and settings"
		>
			<ScanLine size={14} />
			{analyzingBatch
				? 'Stop reading'
				: `Refresh ${referenceCount} saved ${referenceCount === 1 ? 'style' : 'styles'}`}
		</button>
	{/if}
	<div class="upload-note" title="Your reference audio stays in this browser's local storage">
		<LockKeyhole size={13} /> Stays on this computer · MP3 / WAV
	</div>
</div>

<style>
	.reference-uploader {
		position: relative;
		display: grid;
		grid-template-columns: auto 1fr auto;
		align-items: center;
		gap: 1rem;
		padding: 1.15rem;
		border: 1px dashed var(--line-strong);
		border-radius: 0.85rem;
		background: linear-gradient(135deg, rgba(240, 144, 64, 0.1), rgba(255, 255, 255, 0.025));
		transition:
			border-color 180ms ease,
			background 180ms ease,
			transform 180ms ease;
	}
	.reference-uploader::before {
		content: '';
		position: absolute;
		inset: 0;
		border-radius: inherit;
		background: linear-gradient(90deg, transparent, rgba(240, 144, 64, 0.08), transparent);
		transform: translateX(-100%);
		animation: scan-line 5s ease-in-out infinite;
		pointer-events: none;
	}
	.reference-uploader.dragging {
		border-color: var(--accent);
		background: rgba(240, 144, 64, 0.16);
		transform: translateY(-2px);
	}
	.upload-icon {
		display: grid;
		place-items: center;
		width: 3rem;
		height: 3rem;
		border: 1px solid rgba(240, 144, 64, 0.45);
		border-radius: 0.65rem;
		color: var(--accent);
		background: rgba(240, 144, 64, 0.12);
		box-shadow: 0 0 24px rgba(240, 144, 64, 0.12);
	}
	.upload-copy {
		display: flex;
		flex-direction: column;
		gap: 0.25rem;
		min-width: 0;
	}
	.upload-kicker,
	.upload-note {
		font: 0.65rem var(--mono);
		letter-spacing: 0.08em;
		text-transform: uppercase;
		color: var(--muted);
	}
	.upload-kicker {
		color: var(--accent);
	}
	.upload-copy strong {
		font-size: 0.95rem;
		letter-spacing: -0.01em;
	}
	.upload-copy > span:last-child {
		font-size: 0.76rem;
		color: var(--muted);
		white-space: nowrap;
		overflow: hidden;
		text-overflow: ellipsis;
	}
	.signal-dot {
		display: inline-block;
		width: 0.4rem;
		height: 0.4rem;
		margin-right: 0.25rem;
		border-radius: 50%;
		background: var(--ok);
		box-shadow: 0 0 10px var(--ok);
		animation: pulse-dot 1.8s ease-in-out infinite;
	}
	.upload-button {
		position: relative;
		z-index: 1;
		display: inline-flex;
		align-items: center;
		gap: 0.45rem;
		padding: 0.65rem 0.85rem;
		border: 1px solid rgba(240, 144, 64, 0.5);
		border-radius: 0.45rem;
		background: var(--accent);
		color: #171717;
		font: 700 0.72rem var(--mono);
		cursor: pointer;
		white-space: nowrap;
	}
	.upload-actions {
		display: flex;
		align-items: center;
		gap: 0.45rem;
		position: relative;
		z-index: 1;
	}
	.template-button {
		display: inline-flex;
		align-items: center;
		gap: 0.35rem;
		padding: 0.62rem 0.72rem;
		border: 1px solid var(--line-strong);
		border-radius: 0.45rem;
		background: var(--bg-btn);
		color: var(--fg);
		font: 0.68rem var(--mono);
		cursor: pointer;
		white-space: nowrap;
	}
	.template-button:hover:not(:disabled) {
		border-color: var(--accent);
		color: var(--accent);
		background: var(--bg-btn-hover);
	}
	.template-button:disabled {
		opacity: 0.65;
		cursor: wait;
	}
	.batch-analyze {
		position: relative;
		z-index: 1;
		display: inline-flex;
		align-items: center;
		justify-content: center;
		gap: 0.4rem;
		grid-column: 2;
		justify-self: start;
		padding: 0.5rem 0.7rem;
		border: 1px solid color-mix(in srgb, var(--accent) 60%, var(--line));
		border-radius: 0.4rem;
		background: rgba(240, 144, 64, 0.1);
		color: var(--accent);
		font: 0.65rem var(--mono);
		cursor: pointer;
	}
	.batch-analyze:hover:not(:disabled) {
		background: rgba(240, 144, 64, 0.18);
	}
	.batch-analyze:disabled {
		cursor: wait;
		opacity: 0.65;
	}
	.upload-button:hover:not(:disabled) {
		background: var(--accent-hot);
		box-shadow: 0 0 24px rgba(240, 144, 64, 0.22);
	}
	.upload-button:disabled {
		opacity: 0.65;
		cursor: wait;
	}
	.upload-note {
		position: absolute;
		right: 1rem;
		bottom: -1.1rem;
		display: flex;
		align-items: center;
		gap: 0.25rem;
		letter-spacing: 0.03em;
		text-transform: none;
	}
	@keyframes scan-line {
		0%,
		35% {
			transform: translateX(-100%);
		}
		65%,
		100% {
			transform: translateX(100%);
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
	@media (max-width: 720px) {
		.reference-uploader {
			grid-template-columns: auto 1fr;
		}
		.upload-actions {
			grid-column: 2;
			justify-self: start;
		}
	}
</style>
