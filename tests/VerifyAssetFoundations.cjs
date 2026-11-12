// Optional integration check against a real map_asset.json (Node 24+).
// node tests/VerifyAssetFoundations.cjs <map_asset.json> <mapimggen.exe> <output-dir>
const fs = require('node:fs');
const zlib = require('node:zlib');
const cp = require('node:child_process');
const path = require('node:path');
const [databasePath, executable, output] = process.argv.slice(2);
const db = JSON.parse(fs.readFileSync(databasePath, 'utf8'));
const assets = Object.entries(db).filter(([id, a]) => a.bw64 &&
    /家|屋敷|ショップ|倉庫|アパート|教会|橋|城|宿|店/.test(Object.values(a.name || {}).join(' ')));
const dimensions = new Map();
assets.forEach(([id, a]) => {
    const b = zlib.zstdDecompressSync(Buffer.from(a.bw64, 'base64'));
    // This fixture reader supports the native BlockWorld 0x0cae header.
    // Production uses ObjectInput.decode in the editor instead.
    if (b.readUInt16LE(0) !== 0x0cae) throw Error('Unsupported BlockWorld fixture format');
    const name = Object.values(a.name).join(' ');
    const fields = {Id: Number(id.slice(2)), Width: b.readUInt16LE(8), Depth: b.readUInt16LE(12),
        Category: /城壁/.test(name) ? 2 : /橋/.test(name) ? 1 : 0};
    dimensions.set(id, fields);
});
fs.mkdirSync(output, {recursive: true});
const villageParameterIndex=process.argv.indexOf('--village-params');
const villageParameterFile=villageParameterIndex>=0?process.argv[villageParameterIndex+1]:null;
const nativeParameters=villageParameterFile?JSON.parse(fs.readFileSync(villageParameterFile,'utf8')):null;
if(!villageParameterFile) console.log('SKIP native village/town/castle integration: provide --village-params <editor door-aware last_asset_params.json>; CTest covers settlement growth separately');
const types=villageParameterFile?['village','town','castle']:[];
const cases = types.flatMap(type => [0, 6].map(wallHeight => ({type, wallHeight, site: 0})));
if (process.argv.includes('--all-sites')) for (const type of types.filter(t=>t!=='castle'))
    for (let site = 1; site <= 6; ++site) cases.push({type, wallHeight: 0, site});
for (const {type, wallHeight, site} of cases) {
    const name = `${type}_wall${wallHeight}_site${site}`;
    const parameters=['--params-file',villageParameterFile];
    const result = cp.spawnSync(executable, ['-type', type, '-w', '320', '-d', '320', '-seed', '4242',
        '-dir', output, '-out', name, ...parameters, '-p', `wallHeight=${wallHeight}`, '-p', `settlementSite=${site}`,
        '--data', '--plan', '--strict'], {encoding: 'utf8'});
    if (result.status !== 0) throw Error(`${name}: ${result.stdout}\n${result.stderr}`);
    const plan = JSON.parse(fs.readFileSync(path.join(output, `${name}_plan.json`), 'utf8'));
    if (!plan.regions.some(r => r.kind === 'asset_plot')) throw Error('Foundation mode was not applied');
    for (const r of plan.regions) {
        if(!r.kind.startsWith('asset_'))continue;
        const a = dimensions.get(r.role);
        const rotated=(r.rotation||0)%2;
        if (!a || r.x1-r.x0+1 !== (rotated?a.Depth:a.Width) || r.y1-r.y0+1 !== (rotated?a.Width:a.Depth)) throw Error('Asset footprint mismatch');
        if (r.x0 < 0 || r.y0 < 0 || r.x1 >= 320 || r.y1 >= 320) throw Error('Plot outside image');
    }
    const csv = fs.readFileSync(path.join(output, `${name}_biome.csv`), 'utf8');
    if (!wallHeight && plan.regions.some(r => r.kind === 'asset_wall')) throw Error('Unexpected wall plot');
    for (const wall of plan.regions.filter(r => r.kind === 'asset_wall')) {
        let hasWall = false;
        for (let y = wall.y0; y <= wall.y1; ++y) {
            for (let x = wall.x0; x <= wall.x1; ++x) {
                const i = y * plan.width + x;
                if (plan.biomes[i] === 3 || (plan.biomes[i] === 18 && plan.castleDefenseMask[i] === 2)) hasWall = true;
            }
        }
        if (!hasWall) throw Error('Wall reservation removed the existing wall');
    }
    const tiles = csv.trim().split(/[\s,]+/).map(Number);
    if(type!=='castle') {
        const base=Math.max(2,Math.min(6,Math.round(nativeParameters.assetRoadWidth??2)));
        const mains=plan.regions.filter(r=>r.kind==='village_main_road');
        if(mains.length!==Math.max(0,Math.min(4,Math.round(nativeParameters.villageMainRoadCount??(type==='town'?4:3)))))throw Error('Main road count changed');
        if(mains.some(r=>r.width!==base*4)||plan.regions.some(r=>r.kind==='village_middle_road'&&r.width!==base*2))throw Error('Road hierarchy changed');
        for(const r of plan.regions.filter(r=>r.kind==='asset_plot')) {
            let k;
            for(let i=0;i<nativeParameters.assetCount;i++)if('MA'+String(nativeParameters['asset'+i+'Id']).padStart(3,'0')===r.role&&nativeParameters['asset'+i+'Rotation']===r.rotation){k='asset'+i;break;}
            if(!k)throw Error('No matching native rotation');
            const side=nativeParameters[k+'DoorSide'];
            const northKeep=Math.max(0,Math.min(100,Math.round(nativeParameters.northFacingHouseKeepPercent??10)));
            if((side===0&&northKeep===0)||r.doorX!==r.x0+nativeParameters[k+'DoorX']||r.doorY!==r.y0+nativeParameters[k+'DoorY'])throw Error('Disallowed north-facing or misaligned entrance');
            const x=side===1?r.x1+3:side===3?r.x0-3:r.doorX;
            const y=side===2?r.y1+3:side===0?r.y0-3:r.doorY;
            for(let j=0;j<2;j++)if(![5,6,14].includes(plan.biomes[(y+(side%2?j:0))*320+x+(side%2?0:j)]))throw Error('Entrance misses public street');
        }
        if (type === 'village' && plan.biomes.includes(12)) throw Error('Village yards retained stone paving');
    }
    if (!wallHeight && (type==='castle' ? plan.castleDefenseMask.some(b=>b!==0) : tiles.some(b=>b===3||b===4))) throw Error('Outer defenses remain at height zero');
    console.log(`PASS ${name}: ${plan.regions.length} exact-size asset plots; strict 2x2 routing`);
}
