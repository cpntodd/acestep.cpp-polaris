import { strFromU8, strToU8, unzipSync, zipSync } from 'fflate';
import { putSong } from './db.js';
import type { AceRequest, Song } from './types.js';

export const REFERENCE_TEMPLATE_KIND = 'acestep-reference-template';
export const REFERENCE_TEMPLATE_VERSION = 1;

const MAX_TEMPLATE_BYTES = 512 * 1024 * 1024;

interface ReferenceTemplateManifest {
	kind: typeof REFERENCE_TEMPLATE_KIND;
	version: number;
	name: string;
	format: 'mp3' | 'wav';
	created: number;
	caption: string;
	seed: number;
	duration: number;
	request: AceRequest;
	referenceLanguage?: Song['referenceLanguage'];
	stylePrompt?: string;
	analysisState?: Song['analysisState'];
	analysisError?: string;
	analyzedAt?: number;
	audio: string;
	latents?: string;
}

function bytesFromBlob(blob: Blob): Promise<Uint8Array> {
	return blob.arrayBuffer().then((buffer) => new Uint8Array(buffer));
}

function blobFromBytes(bytes: Uint8Array, type: string): Blob {
	return new Blob([bytes.slice().buffer], { type });
}

function jsonFromArchive<T>(archive: Record<string, Uint8Array>, path: string): T {
	const bytes = archive[path];
	if (!bytes) throw new Error(`Template is missing ${path}`);
	try {
		return JSON.parse(strFromU8(bytes)) as T;
	} catch {
		throw new Error(`Template contains invalid ${path}`);
	}
}

function isRecord(value: unknown): value is Record<string, unknown> {
	return typeof value === 'object' && value !== null;
}

function safeName(value: string): string {
	return value.replace(/[\\/:*?"<>|\x00-\x1f]/g, '').trim() || 'reference-template';
}

export async function exportReferenceTemplate(song: Song): Promise<Blob> {
	if (song.source !== 'upload') {
		throw new Error('Only uploaded reference tracks can be exported as templates');
	}

	const format: 'mp3' | 'wav' = song.format.toLowerCase().startsWith('wav') ? 'wav' : 'mp3';
	const audioPath = `audio.${format}`;
	const latentsPath = song.latents ? 'latents.vae' : undefined;
	const manifest: ReferenceTemplateManifest = {
		kind: REFERENCE_TEMPLATE_KIND,
		version: REFERENCE_TEMPLATE_VERSION,
		name: song.name,
		format,
		created: song.created,
		caption: song.caption || song.request.caption || '',
		seed: song.seed || 0,
		duration: song.duration || song.request.duration || 0,
		request: song.request,
		referenceLanguage: song.referenceLanguage,
		stylePrompt: song.stylePrompt,
		analysisState: song.analysisState,
		analysisError: song.analysisError,
		analyzedAt: song.analyzedAt,
		audio: audioPath,
		latents: latentsPath
	};

	const files: Record<string, Uint8Array> = {
		'manifest.json': strToU8(JSON.stringify(manifest, null, 2)),
		'settings.json': strToU8(JSON.stringify(song.request, null, 2)),
		'prompt.txt': strToU8(song.stylePrompt || song.request.caption || song.caption || ''),
		[audioPath]: await bytesFromBlob(song.audio)
	};
	if (song.latents && latentsPath) {
		files[latentsPath] = await bytesFromBlob(song.latents);
	}

	return new Blob([zipSync(files, { level: 0 })], { type: 'application/zip' });
}

export function isReferenceTemplate(file: File): boolean {
	return file.name.toLowerCase().endsWith('.zip') || file.type === 'application/zip';
}

export async function importReferenceTemplate(file: File): Promise<Song> {
	if (file.size === 0) throw new Error('The selected template is empty');
	if (file.size > MAX_TEMPLATE_BYTES) throw new Error('Template ZIP is larger than 512 MB');

	let archive: Record<string, Uint8Array>;
	try {
		archive = unzipSync(new Uint8Array(await file.arrayBuffer()));
	} catch {
		throw new Error('Could not open the template ZIP');
	}

	const manifest = jsonFromArchive<Partial<ReferenceTemplateManifest>>(archive, 'manifest.json');
	if (
		manifest.kind !== REFERENCE_TEMPLATE_KIND ||
		manifest.version !== REFERENCE_TEMPLATE_VERSION
	) {
		throw new Error('Unsupported ACE-Step reference template');
	}
	if (!manifest.request || !isRecord(manifest.request)) {
		throw new Error('Template is missing its request settings');
	}

	const format: 'mp3' | 'wav' = manifest.format === 'wav' ? 'wav' : 'mp3';
	const audioPath = typeof manifest.audio === 'string' ? manifest.audio : `audio.${format}`;
	const audioBytes = archive[audioPath];
	if (!audioBytes || audioBytes.length === 0)
		throw new Error('Template is missing its reference audio');

	const request = {
		...manifest.request,
		caption: String(manifest.request.caption || '')
	} as AceRequest;
	const latentsBytes = manifest.latents ? archive[manifest.latents] : undefined;
	const stylePrompt = typeof manifest.stylePrompt === 'string' ? manifest.stylePrompt : undefined;
	const analysisState = manifest.analysisState === 'ready' || stylePrompt ? 'ready' : 'unscanned';
	const song: Song = {
		name: safeName(
			typeof manifest.name === 'string' ? manifest.name : file.name.replace(/\.zip$/i, '')
		),
		format,
		created: typeof manifest.created === 'number' ? manifest.created : Date.now(),
		caption: typeof manifest.caption === 'string' ? manifest.caption : request.caption,
		seed: typeof manifest.seed === 'number' ? manifest.seed : 0,
		duration: typeof manifest.duration === 'number' ? manifest.duration : request.duration || 0,
		request,
		audio: blobFromBytes(audioBytes, format === 'wav' ? 'audio/wav' : 'audio/mpeg'),
		source: 'upload',
		referenceLanguage:
			manifest.referenceLanguage === 'mk' || request.vocal_language === 'mk' ? 'mk' : 'auto',
		stylePrompt,
		analysisState,
		analysisError: typeof manifest.analysisError === 'string' ? manifest.analysisError : undefined,
		analyzedAt: typeof manifest.analyzedAt === 'number' ? manifest.analyzedAt : undefined,
		latents: latentsBytes ? blobFromBytes(latentsBytes, 'application/octet-stream') : undefined
	};

	song.id = await putSong(song);
	return song;
}

export function referenceTemplateFilename(song: Song): string {
	return `${safeName(song.name)}.acestep-template.zip`;
}
