// Validate all three settlement types against the editor's actual asset pool.
const fs=require('node:fs'),assert=require('node:assert/strict');
const [planFile,parameterFile,reportFile]=process.argv.slice(2);
const p=JSON.parse(fs.readFileSync(planFile,'utf8')),a=JSON.parse(fs.readFileSync(parameterFile,'utf8'));
const homes=p.regions.filter(r=>r.kind==='asset_plot'),lots=p.regions.filter(r=>r.kind==='village_open_space');
assert(homes.length>0,'No actual buildings');
let large=0,north=0,sideways=0;const facilityCounts=new Map();
const occupied=new Set();
for(const r of [...homes,...lots]){
 for(let y=r.y0-2;y<=r.y1+2;y++)for(let x=r.x0-2;x<=r.x1+2;x++){
  assert(x>=0&&y>=0&&x<p.width&&y<p.depth,'Lot outside canvas');
  const i=y*p.width+x;assert(!occupied.has(i),'Overlapping building/park setbacks');occupied.add(i);
  if(p.settlementMask?.length)assert.equal(p.settlementMask[i],1,'Lot outside development area');
 }
 if(r.kind!=='asset_plot'){assert.equal(r.doorX,-1);continue;}
 let match;
 for(let i=0;i<a.assetCount;i++){
  const k='asset'+i;
  if(r.role==='MA'+String(a[k+'Id']).padStart(3,'0')&&r.rotation===a[k+'Rotation']){match=k;break;}
 }
 assert(match,'No matching native asset orientation');
 assert.equal(r.x1-r.x0+1,a[match+'Width']);assert.equal(r.y1-r.y0+1,a[match+'Depth']);
 assert.equal(r.doorX,r.x0+a[match+'DoorX']);assert.equal(r.doorY,r.y0+a[match+'DoorY']);
 const side=a[match+'DoorSide'];
 if(Math.max(a[match+'Width'],a[match+'Depth'])>12){large++;assert([0,2].includes(side),'Oversized sideways building');}
 if(side===0){north++;assert.notEqual(a.northFacingHouseKeepPercent,0,'North house retained at 0%');}
 if(side%2){sideways++;assert.notEqual(a.sideFacingHouseKeepPercent,0,'Side house retained at 0%');}
 const rx=side===1?r.x1+3:side===3?r.x0-3:r.doorX,ry=side===0?r.y0-3:side===2?r.y1+3:r.doorY;
 for(let k=0;k<2;k++)assert([5,6,14].includes(p.biomes[(ry+(side%2?k:0))*p.width+rx+(side%2?0:k)]),'Entrance misses its frontage street');
 const group=a[match+'LimitGroup'] ?? a[match+'Id']+1000,limit=a[match+'MaxCount'];
 if(limit>0){const count=(facilityCounts.get(group)||0)+1;facilityCounts.set(group,count);assert(count<=limit,'Unique facility count exceeded');}
}
const markers=p.markers.filter(m=>['asset_approach','village_center','village_open_space_approach','castle_town_connection'].includes(m.kind)||m.role==='keep_public_entry');
const pass=(x,y)=>x>=0&&y>=0&&x+1<p.width&&y+1<p.depth&&
 [0,1,p.width,p.width+1].every(k=>[5,6,14].includes(p.biomes[y*p.width+x+k]));
assert(markers.length>0&&pass(markers[0].x,markers[0].y));
const seen=new Set([markers[0].y*p.width+markers[0].x]),queue=[...seen];
for(let k=0;k<queue.length;k++){const i=queue[k];for(const [dx,dy] of [[0,-1],[1,0],[0,1],[-1,0]]){
 const x=i%p.width+dx,y=Math.floor(i/p.width)+dy,j=y*p.width+x;
 if(!seen.has(j)&&pass(x,y)){seen.add(j);queue.push(j);}
}}
assert(markers.every(m=>seen.has(m.y*p.width+m.x)),'Entrance is unreachable by a 2x2 human');
const trials=(p.castleHistory||[]).filter(s=>s.phase==='layout_trial').map(s=>s.additions);
if(trials.length)assert.equal(homes.length,Math.max(...trials),'Castle selection missed the candidate with most usable houses');
const report={passed:true,seed:p.seed,type:p.type,mode:p.archetype,homes:homes.length,largeBuildings:large,north,sideways,facilityCounts:Object.fromEntries(facilityCounts),
 openLots:lots.length,target:a.assetPlotCount,reachableEntrances:markers.length,castleTrialHomes:trials};
if(reportFile)fs.writeFileSync(reportFile,JSON.stringify(report,null,2));console.log(JSON.stringify(report));
