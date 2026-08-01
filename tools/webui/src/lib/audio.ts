// audio.ts: shared AudioContext and playback sync
//
// All Waveform components share a single AudioContext so they run on
// the same sample clock. When a track starts playing, it checks if
// another track with the same duration is already playing and syncs
// to its position (same duration = same song variation).

let ctx: AudioContext | null = null;

// shared AudioContext, created on first use
export function getContext(): AudioContext {
	if (!ctx) {
		ctx = new AudioContext();
	}
	return ctx;
}

// Combine several local reference recordings into one stereo source context.
// ACE-Step accepts one source context per request; mixing here lets multiple
// selected songs contribute melody, riffs, harmony, and rhythm together while
// keeping all source audio local to the browser.
export async function mixAudioBlobs(blobs: Blob[]): Promise<Blob> {
	if (blobs.length === 0) throw new Error('Choose at least one source song');
	if (blobs.length === 1) return blobs[0];

	const decoded = await Promise.all(
		blobs.map(async (blob) => {
			const bytes = await blob.arrayBuffer();
			return getContext().decodeAudioData(bytes);
		})
	);
	const sampleRate = 48000;
	const maxSeconds = 600;
	const frameCount = Math.max(
		1,
		Math.min(
			maxSeconds * sampleRate,
			Math.max(...decoded.map((buffer) => buffer.duration)) * sampleRate
		)
	);
	const offline = new OfflineAudioContext(2, Math.ceil(frameCount), sampleRate);
	const gain = 1 / Math.sqrt(decoded.length);

	for (const buffer of decoded) {
		const source = offline.createBufferSource();
		const level = offline.createGain();
		source.buffer = buffer;
		level.gain.value = gain;
		source.connect(level).connect(offline.destination);
		source.start(0);
	}

	const rendered = await offline.startRendering();
	return audioBufferToWav(rendered);
}

function audioBufferToWav(buffer: AudioBuffer): Blob {
	const channels = Math.min(2, buffer.numberOfChannels);
	const frames = buffer.length;
	const bytesPerSample = 2;
	const dataSize = frames * channels * bytesPerSample;
	const out = new ArrayBuffer(44 + dataSize);
	const view = new DataView(out);
	const write = (offset: number, value: string) => {
		for (let i = 0; i < value.length; i++) view.setUint8(offset + i, value.charCodeAt(i));
	};
	write(0, 'RIFF');
	view.setUint32(4, 36 + dataSize, true);
	write(8, 'WAVE');
	write(12, 'fmt ');
	view.setUint32(16, 16, true);
	view.setUint16(20, 1, true);
	view.setUint16(22, channels, true);
	view.setUint32(24, buffer.sampleRate, true);
	view.setUint32(28, buffer.sampleRate * channels * bytesPerSample, true);
	view.setUint16(32, channels * bytesPerSample, true);
	view.setUint16(34, 16, true);
	write(36, 'data');
	view.setUint32(40, dataSize, true);

	const channelData = Array.from({ length: channels }, (_, channel) =>
		buffer.getChannelData(channel)
	);
	let offset = 44;
	for (let frame = 0; frame < frames; frame++) {
		for (let channel = 0; channel < channels; channel++) {
			const sample = Math.max(-1, Math.min(1, channelData[channel][frame]));
			view.setInt16(offset, sample < 0 ? sample * 0x8000 : sample * 0x7fff, true);
			offset += 2;
		}
	}
	return new Blob([out], { type: 'audio/wav' });
}

// playing track registry for auto sync

interface PlayingTrack {
	duration: number;
	getTime: () => number;
}

const tracks = new Map<number, PlayingTrack>();
let nextId = 0;

// register a playing track. returns id for unregister.
export function registerPlaying(duration: number, getTime: () => number): number {
	const id = nextId++;
	tracks.set(id, { duration, getTime });
	return id;
}

// unregister a track when playback stops
export function unregisterPlaying(id: number) {
	tracks.delete(id);
}

// number of tracks currently playing (for volume division)
export function playingCount(): number {
	return tracks.size || 1;
}

// find the position of any playing track with the same duration.
// returns its current time, or -1 if no match.
export function findSyncPosition(duration: number, excludeId: number): number {
	for (const [id, track] of tracks) {
		if (id !== excludeId && Math.abs(track.duration - duration) < 0.1) {
			return track.getTime();
		}
	}
	return -1;
}
