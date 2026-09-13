// Optional asset maintenance only: node scripts/generate_app_icon.mjs [--check]
// Requires Node.js and ImageMagick 7; neither is needed to build the app.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { copyFileSync, mkdtempSync, readFileSync, readdirSync, rmdirSync, unlinkSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256];
const beige = [238, 236, 223, 255];
assert.ok(process.argv.slice(2).every(arg => arg === '--check'), 'Only --check is supported');
const checkOnly = process.argv.includes('--check');
const magick = (...args) => execFileSync('magick', args, { cwd: root, maxBuffer: 16 * 1024 * 1024 });
const transparent = readFileSync(join(root, 'resources/greenflame-transparent.svg'), 'utf8');
const opaque = readFileSync(join(root, 'resources/greenflame.svg'), 'utf8');
assert.equal(opaque.replace('    <rect id="background" width="256" height="256" fill="#eeecdf"/>\n', ''), transparent);
const temp = mkdtempSync(join(tmpdir(), 'greenflame-icon-'));
try {
    // Clip rasterizer halos outside the exact bracket bounds before reduction.
    const master = join(temp, 'master.png');
    magick('-density', '384', '-background', 'none', 'resources/greenflame-transparent.svg',
        '-resize', '1024x1024!', '-crop', '896x896+64+64', '+repage',
        '-bordercolor', 'none', '-border', '64', '-strip', master);
    const frames = sizes.map(size => {
        const file = join(temp, size + '.png');
        magick(master, '-filter', 'Box', '-resize', size + 'x' + size + '!',
            '-background', '#eeecdf', '-alpha', 'remove', '-alpha', 'off', '-depth', '8', '-strip', file);
        return file;
    });
    const icon = join(temp, 'greenflame.ico');
    const favicon = join(temp, 'favicon.ico');
    magick(...frames, '-depth', '8', icon);
    magick(...[16, 32, 48].map(size => join(temp, size + '.png')), '-depth', '8', favicon);
    const outputs = [
        [icon, 'resources/greenflame.ico'],
        [favicon, 'images/favicon.ico'],
        [join(temp, '256.png'), 'images/greenflame_256.png'],
    ];
    if (!checkOnly) for (const [source, target] of outputs) copyFileSync(source, join(root, target));
    for (const [source, target] of outputs) {
        const actual = magick(target, '-depth', '8', 'rgba:-');
        assert.deepEqual(actual, magick(source, '-depth', '8', 'rgba:-'), target + ' must match the SVG render');
    }
    const actualSizes = magick('resources/greenflame.ico', '-format', '%w ', 'info:').toString().trim().split(/\s+/).map(Number);
    assert.deepEqual(actualSizes, sizes, 'ICO must include every required size');
    for (const [index, size] of sizes.entries()) {
        const pixels = magick('resources/greenflame.ico[' + index + ']', '-depth', '8', 'rgba:-');
        assert.equal(pixels.length, size * size * 4);
        let occupied = 0;
        for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
            const pixel = [...pixels.subarray((y * size + x) * 4, (y * size + x + 1) * 4)];
            assert.equal(pixel[3], 255, 'Beige icon must be fully opaque');
            const margin = Math.floor(size / 16);
            if (x < margin || y < margin || x >= size - margin || y >= size - margin) {
                assert.deepEqual(pixel, beige, size + 'px icon must retain its clear beige border');
            }
            if (!pixel.every((value, channel) => value === beige[channel])) occupied++;
        }
        assert.ok(occupied > 0, 'Artwork must not be blank');
    }
    console.log('Verified beige ICO sizes ' + sizes.join(', ') + '; favicon, README image, opacity, clear borders, and matching SVG sources.');
} finally {
    for (const file of readdirSync(temp)) unlinkSync(join(temp, file));
    rmdirSync(temp);
}
