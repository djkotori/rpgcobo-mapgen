// Verify a selected editor Castle plan against its live native asset parameters.
const fs=require('node:fs'),assert=require('node:assert/strict');
const [planFile,paramFile,reportFile]=process.argv.slice(2);
const p=JSON.parse(fs.readFileSync(planFile,'utf8')),a=JSON.parse(fs.readFileSync(paramFile,'utf8'));
assert.equal(p.selectionReason,'castle_and_asset_town');
assert(p.regions.some(r=>r.kind==='keep'));assert(p.regions.some(r=>r.kind==='room'));
const homes=p.regions.filter(r=>r.kind==='asset_plot');assert(homes.length>0);
const keep=p.regions.find(r=>r.kind==='keep');
for(const r of homes){
 let match=false;
 for(let i=0;i<a.assetCount;i++){
  const k='asset'+i,id='MA'+String(a[k+'Id']).padStart(3,'0');
  if(id!==r.role||a[k+'Rotation']!==r.rotation)continue;
  assert.equal(r.x1-r.x0+1,a[k+'Width']);assert.equal(r.y1-r.y0+1,a[k+'Depth']);
  assert.equal(r.doorX-r.x0,a[k+'DoorX']);assert.equal(r.doorY-r.y0,a[k+'DoorY']);match=true;
  if(Math.max(a[k+'Width'],a[k+'Depth'])>12)assert([0,2].includes(a[k+'DoorSide']),'Oversized sideways building');
 }
 assert(match,'No matching native asset');
 for(let y=r.y0-2;y<=r.y1+2;y++)for(let x=r.x0-2;x<=r.x1+2;x++)
  assert.equal(p.settlementMask[y*p.width+x],1,'House outside original bailey development area');
 assert(r.x1<keep.x0||r.x0>keep.x1||r.y1<keep.y0||r.y0>keep.y1,'House overlaps castle keep');
}
assert(p.castleHistory.some(s=>s.phase==='human_scale'&&s.additions===2),'Castle is not based on 2x2 humans');
assert(p.castleHistory.some(s=>s.phase==='keep_scale'&&s.additions===2),'Keep was not generated at double scale');
const rooms=p.regions.filter(r=>r.kind==='room'),wall=(x,y)=>[3,4,9].includes(p.biomes[y*p.width+x]);
let sharedPartitions=0;
for(const a of rooms)for(const b of rooms){
 if(a.x1+1===b.x0)for(let y=Math.max(a.y0,b.y0)+2;y<=Math.min(a.y1,b.y1)-2;y+=2){
  assert(!wall(a.x1,y)&&!wall(b.x0+2,y),'Vertical room boundary has duplicate walls');
  if(wall(b.x0,y)){assert(wall(b.x0+1,y));sharedPartitions++;}
 }
 if(a.y1+1===b.y0)for(let x=Math.max(a.x0,b.x0)+2;x<=Math.min(a.x1,b.x1)-2;x+=2){
  assert(!wall(x,a.y1)&&!wall(x,b.y0+2),'Horizontal room boundary has duplicate walls');
  if(wall(x,b.y0)){assert(wall(x,b.y0+1));sharedPartitions++;}
 }
}
assert(sharedPartitions>0,'Missing shared room partitions');
const entry=p.markers.find(m=>m.role==='keep_public_entry');assert(entry);
const gates=p.regions.filter(r=>r.kind==='castle_front_door');assert.equal(gates.length,1);
for(const r of gates){assert.equal(r.x1-r.x0+1,10);assert.equal(r.y1-r.y0+1,2);
 for(let y=r.y0;y<=r.y1;y++)for(let x=r.x0;x<=r.x1;x++)assert.equal(p.biomes[y*p.width+x],6,'Great front gate opening blocked');
}
for(let i=0;i<p.biomes.length;i++)if(p.settlementMask[i])assert(![3,4,9].includes(p.biomes[i]),'Stray town wall inside bailey');
if(process.argv[5]){
 const actual=JSON.parse(fs.readFileSync(process.argv[5],'utf8'));assert.equal(actual.seed,p.seed);assert.equal(actual.doors.length,1);
 assert.equal(actual.wallBlock,actual.innerWallBlock,'Keep still uses black shell blocks');
 assert.equal(actual.doors[0].width,10);assert.equal(actual.doors[0].height,7);assert.equal(actual.doors[0].fbparam,16);
}
const insidePass=(x,y)=>x>=0&&y>=0&&x+1<p.width&&y+1<p.depth&&
 [0,1,p.width,p.width+1].every(k=>[5,6,10,11,12,14].includes(p.biomes[y*p.width+x+k]));
assert(insidePass(entry.x,entry.y),'Keep entry is too narrow');
const insideSeen=new Set([entry.y*p.width+entry.x]),insideQ=[...insideSeen];
for(let k=0;k<insideQ.length;k++){const i=insideQ[k];for(const [dx,dy] of [[1,0],[-1,0],[0,1],[0,-1]]){
 const x=i%p.width+dx,y=Math.floor(i/p.width)+dy,j=y*p.width+x;
 if(!insideSeen.has(j)&&insidePass(x,y)){insideSeen.add(j);insideQ.push(j);}
}}
for(const r of rooms){let reached=false;for(let y=r.y0+2;y<r.y1;y++)for(let x=r.x0+2;x<r.x1;x++)reached||=insideSeen.has(y*p.width+x);
 assert(reached,'Room is unreachable for 2x2 human: '+r.role);
}
for(const m of p.markers.filter(m=>['village_center','castle_town_connection'].includes(m.kind)))
 assert(insideSeen.has(m.y*p.width+m.x),'Keep entry is disconnected from the castle town or main gate');
const markers=p.markers.filter(m=>['village_center','asset_approach','village_open_space_approach','castle_town_connection'].includes(m.kind));
function pass(x,y){if(x<0||y<0||x+1>=p.width||y+1>=p.depth)return false;return [0,1,p.width,p.width+1].every(k=>[5,6,14].includes(p.biomes[y*p.width+x+k]));}
const seen=new Set(),q=[markers[0].y*p.width+markers[0].x];seen.add(q[0]);
for(let k=0;k<q.length;k++){const i=q[k];for(const [dx,dy] of [[1,0],[-1,0],[0,1],[0,-1]]){const x=i%p.width+dx,y=Math.floor(i/p.width)+dy,j=y*p.width+x;if(!seen.has(j)&&pass(x,y)){seen.add(j);q.push(j);}}}
assert(markers.every(m=>seen.has(m.y*p.width+m.x)),'Gate/town entrance disconnected for 2x2 human');
const report={passed:true,seed:p.seed,mode:p.archetype,homes:homes.length,rooms:rooms.length,keepWidth:keep.x1-keep.x0+1,keepDepth:keep.y1-keep.y0+1,sharedPartitions,reachableMarkers:markers.length,greatGates:gates.length};
if(reportFile)fs.writeFileSync(reportFile,JSON.stringify(report,null,2));console.log(report);
