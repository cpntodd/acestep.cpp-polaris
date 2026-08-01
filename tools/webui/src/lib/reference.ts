import { jobResultUnderstand, understandSubmit, pollJob } from './api.js';
import { putSong } from './db.js';
import type { Song } from './types.js';

const AUDIO_TYPES: Record<string, string> = {
	mp3: 'audio/mpeg',
	wav: 'audio/wav'
};

export function isReferenceAudio(file: File): boolean {
	const ext = file.name.split('.').pop()?.toLowerCase() || '';
	return ext in AUDIO_TYPES;
}

// Importing is deliberately a browser-local operation. The caller decides
// when to post the blob to /understand or /synth.
export async function storeReferenceTrack(file: File): Promise<Song> {
	const ext = file.name.split('.').pop()?.toLowerCase() || '';
	if (!(ext in AUDIO_TYPES)) throw new Error('Use an MP3 or WAV reference track');
	if (file.size === 0) throw new Error('The selected audio file is empty');

	const audio = new Blob([await file.arrayBuffer()], { type: file.type || AUDIO_TYPES[ext] });
	const name = file.name.replace(/\.(mp3|wav)$/i, '') || 'Reference track';
	const song: Song = {
		name,
		format: ext,
		created: Date.now(),
		caption: '',
		seed: 0,
		duration: 0,
		request: { caption: '' },
		audio,
		source: 'upload',
		analysisState: 'unscanned'
	};
	song.id = await putSong(song);
	return song;
}

// Read one saved reference track locally and turn the result into a reusable
// style profile. The caller persists the updated Song so this helper can be
// shared by the card-level action and the batch analyser.
export async function analyzeReferenceSong(
	song: Song,
	lmModel?: string,
	synthModel?: string,
	options?: {
		onJobId?: (id: string) => void;
		isCancelled?: () => boolean;
	}
): Promise<{ requests: Song['request'][] }> {
	if (song.source !== 'upload') throw new Error('Only reference tracks can be analyzed');
	song.analysisState = 'analyzing';
	song.analysisError = '';
	try {
		const jobId = await understandSubmit(
			song.latents ? null : song.audio,
			song.latents ?? null,
			lmModel,
			synthModel
		);
		options?.onJobId?.(jobId);
		if (options?.isCancelled?.()) throw new Error('Analysis stopped');
		await pollJob(jobId);
		const { requests, latents } = await jobResultUnderstand(jobId);
		if (requests.length === 0) throw new Error('The style reader returned no result');
		const detected = requests[0];
		const newRequest = {
			...detected,
			task_type: song.request.task_type || 'text2music',
			synth_model: song.request.synth_model || synthModel,
			lm_model: song.request.lm_model || lmModel,
			vae: song.request.vae
		};
		if (latents) song.latents = latents;
		song.request = newRequest;
		song.caption = newRequest.caption ?? song.caption;
		song.stylePrompt = newRequest.caption ?? '';
		song.seed = newRequest.seed ?? song.seed;
		song.duration = newRequest.duration ?? song.duration;
		song.analysisState = 'ready';
		song.analysisError = '';
		song.analyzedAt = Date.now();
		return { requests };
	} catch (error) {
		if (options?.isCancelled?.()) {
			song.analysisState = 'unscanned';
			song.analysisError = '';
		} else {
			song.analysisState = 'error';
			song.analysisError = error instanceof Error ? error.message : String(error);
		}
		throw error;
	}
}
