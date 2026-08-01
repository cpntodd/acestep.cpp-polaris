import type { AceRequest, AceProps, Song, SystemMetrics } from './types.js';

const STORAGE_KEY = 'ace';

interface Saved {
	name: string;
	volume: number;
	format: string;
	dark: boolean;
	logsOpen: boolean;
	referenceLanguage: 'auto' | 'mk';
	request: AceRequest;
}

function targetLanguage(value: unknown): string {
	const language = String(value ?? '')
		.trim()
		.toLowerCase();
	if (language === 'english' || language === 'english (en)' || language === 'eng') return 'en';
	if (
		language === 'macedonian' ||
		language === 'macedonian (mk)' ||
		language === 'mkd' ||
		language === 'mac'
	) {
		return 'mk';
	}
	return language === 'en' || language === 'mk' || language === 'unknown' ? language : '';
}

function targetRequest(request: AceRequest): AceRequest {
	const next = { ...request };
	const language = targetLanguage(next.vocal_language);
	if (language) next.vocal_language = language;
	else delete next.vocal_language;
	return next;
}

function load(): Saved {
	try {
		const raw = localStorage.getItem(STORAGE_KEY);
		if (raw) {
			const parsed = JSON.parse(raw);
			return {
				name: parsed.name || '',
				volume: parsed.volume ?? 0.5,
				format: ['mp3', 'wav16', 'wav24', 'wav32'].includes(parsed.format) ? parsed.format : 'mp3',
				dark: parsed.dark ?? true,
				logsOpen: parsed.logsOpen ?? true,
				referenceLanguage: parsed.referenceLanguage === 'auto' ? 'auto' : 'mk',
				request: targetRequest(parsed.request || { caption: '', use_cot_caption: true })
			};
		}
	} catch {
		// corrupt or unavailable
	}
	return {
		name: '',
		volume: 0.5,
		format: 'mp3',
		dark: true,
		logsOpen: true,
		referenceLanguage: 'mk',
		request: { caption: '', use_cot_caption: true }
	};
}

const saved = load();

export const app = $state({
	name: saved.name,
	volume: saved.volume,
	format: saved.format,
	dark: saved.dark,
	logsOpen: saved.logsOpen,
	referenceLanguage: saved.referenceLanguage,
	request: saved.request as AceRequest,
	songs: [] as Song[],
	props: null as AceProps | null,
	metrics: null as SystemMetrics | null,
	toast: '' as string,
	toastOk: false,
	pendingRequests: [] as AceRequest[],
	pendingIndex: 0,
	refSongId: null as number | null,
	srcSongIds: [] as number[],
	srcRangeStart: null as number | null,
	srcRangeEnd: null as number | null
});

let toastTimer = 0;

export function toast(msg: string, ms = 4000, ok = false) {
	clearTimeout(toastTimer);
	app.toast = msg;
	app.toastOk = ok;
	toastTimer = setTimeout(() => {
		app.toast = '';
	}, ms) as unknown as number;
}

// overwrite app.request, preserving model routing fields unless the
// incoming request provides them (non-empty string / non-null number).
export function setRequest(incoming: AceRequest) {
	incoming = targetRequest(incoming);
	if (!incoming.synth_model) incoming.synth_model = app.request.synth_model;
	if (!incoming.lm_model) incoming.lm_model = app.request.lm_model;
	if (!incoming.adapter) incoming.adapter = app.request.adapter;
	if (incoming.adapter_scale == null) incoming.adapter_scale = app.request.adapter_scale;
	if (!incoming.vae) incoming.vae = app.request.vae;
	if (incoming.use_cot_caption === undefined) incoming.use_cot_caption = true;
	app.request = incoming;
	app.srcRangeStart = incoming.repainting_start ?? null;
	app.srcRangeEnd = incoming.repainting_end ?? null;
}

// sync srcRange to request fields (srcRange is the UI source of truth,
// request fields are the serialization layer read by FIELDS helpers)
$effect.root(() => {
	$effect(() => {
		app.request.repainting_start = app.srcRangeStart ?? undefined;
		app.request.repainting_end = app.srcRangeEnd ?? undefined;
	});
});

// persist on every change
$effect.root(() => {
	$effect(() => {
		const data: Saved = {
			name: app.name,
			volume: app.volume,
			format: app.format,
			dark: app.dark,
			logsOpen: app.logsOpen,
			referenceLanguage: app.referenceLanguage,
			request: app.request
		};
		localStorage.setItem(STORAGE_KEY, JSON.stringify(data));
	});
});
