<script lang="ts">
	import { app } from '../lib/state.svelte.js';
</script>

{#if app.toast}
	<div class="toast" class:ok={app.toastOk}>{app.toast}</div>
{/if}

<style>
	.toast {
		position: fixed;
		bottom: 1.5rem;
		left: 50%;
		transform: translateX(-50%);
		background: var(--surface-raised);
		border: 1px solid var(--error);
		color: #fff;
		font-size: 0.85rem;
		padding: 0.5rem 1.2rem;
		border-radius: 0.45rem;
		box-shadow: 0 15px 45px rgba(0, 0, 0, 0.35);
		font-family: var(--mono);
		letter-spacing: 0.02em;
		z-index: 9999;
		pointer-events: none;
		animation: fade-in 0.15s ease-out;
	}
	.toast.ok {
		border-color: var(--ok);
		color: var(--fg);
	}
	@keyframes fade-in {
		from {
			opacity: 0;
			transform: translateX(-50%) translateY(0.5rem);
		}
		to {
			opacity: 1;
			transform: translateX(-50%) translateY(0);
		}
	}
</style>
