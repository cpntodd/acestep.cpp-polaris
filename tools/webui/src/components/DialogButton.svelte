<script lang="ts">
	import type { Snippet } from 'svelte';

	let {
		onclick,
		children
	}: {
		onclick: (e: MouseEvent) => void;
		children: Snippet;
	} = $props();
</script>

<button type="button" {onclick}>{@render children()}</button>

<style>
	button {
		background: var(--bg-btn);
		border: 1px solid var(--line);
		cursor: pointer;
		padding: 0.38rem 0.7rem;
		color: var(--fg);
		font-size: 0.8rem;
		border-radius: 0.35rem;
	}
	button:hover {
		background: var(--bg-btn-hover);
		border-color: var(--accent);
		color: var(--accent);
	}
</style>
