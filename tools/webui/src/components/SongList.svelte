<script lang="ts">
	import { AudioLines, Library, Sparkles } from '@lucide/svelte';
	import { app } from '../lib/state.svelte.js';
	import SongCard from './SongCard.svelte';
	import LogCard from './LogCard.svelte';
</script>

<div class="song-list">
	{#if app.songs.length === 0}
		<div class="empty-library">
			<div class="empty-icon"><Library size={22} /></div>
			<strong>Your music library is empty</strong>
			<p>
				Drop an MP3 or WAV above. It stays on this computer until you choose to study it or use it
				in a new song.
			</p>
			<div class="empty-hint"><AudioLines size={13} /> your audio stays private</div>
		</div>
	{:else}
		<div class="library-toolbar">
			<span><Sparkles size={13} /> Newest first</span>
			<span>{app.songs.length} saved {app.songs.length === 1 ? 'song' : 'songs'}</span>
		</div>
		{#each app.songs as song (song.id)}
			<SongCard {song} />
		{/each}
	{/if}
	<LogCard />
</div>

<style>
	.song-list {
		display: flex;
		flex-direction: column;
		gap: 0.75rem;
		overflow-y: auto;
	}
	.library-toolbar {
		display: flex;
		justify-content: space-between;
		align-items: center;
		padding: 0.1rem 0.15rem 0.2rem;
		color: var(--muted);
		font: 0.6rem var(--mono);
		text-transform: uppercase;
		letter-spacing: 0.05em;
	}
	.library-toolbar span {
		display: inline-flex;
		align-items: center;
		gap: 0.3rem;
	}
	.library-toolbar span:first-child {
		color: var(--accent);
	}
	.empty-library {
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 0.55rem;
		padding: 4rem 2rem;
		border: 1px dashed var(--line-strong);
		border-radius: 0.65rem;
		text-align: center;
		color: var(--muted);
	}
	.empty-icon {
		display: grid;
		place-items: center;
		width: 3rem;
		height: 3rem;
		border: 1px solid var(--line);
		border-radius: 0.6rem;
		color: var(--accent);
		background: rgba(240, 144, 64, 0.1);
	}
	.empty-library strong {
		color: var(--fg);
		font-size: 0.9rem;
	}
	.empty-library p {
		max-width: 23rem;
		font-size: 0.75rem;
		line-height: 1.55;
	}
	.empty-hint {
		display: inline-flex;
		align-items: center;
		gap: 0.3rem;
		margin-top: 0.25rem;
		font: 0.6rem var(--mono);
		color: var(--faint);
	}
</style>
