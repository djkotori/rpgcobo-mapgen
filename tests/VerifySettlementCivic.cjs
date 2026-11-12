const fs=require('node:fs'),assert=require('node:assert/strict');
const [planPath,paramsPath,resourcePath,reportPath,auditPath]=process.argv.slice(2);
const p=JSON.parse(fs.readFileSync(planPath,'utf8')),a=JSON.parse(fs.readFileSync(paramsPath,'utf8')),db=JSON.parse(fs.readFileSync(resourcePath,'utf8'));
const centres=p.regions.filter(r=>r.kind==='village_center'),facilities=p.regions.filter(r=>r.kind==='settlement_facility');
const mayors=p.regions.filter(r=>r.kind==='asset_plot'&&r.residentProfile==='village_head');
assert.equal(centres.length,1);const centre=p.markers.find(m=>m.kind==='village_center');assert(centre);
const expected=['community_well'];if(p.countryWealth>=1)expected.push('public_bench');if(p.countryWealth>=1.5)expected.push('square_lamp');
if(p.countryWealth>=2)expected.push('market_stall','civic_statue');
assert.deepEqual(facilities.map(r=>r.role).sort(),expected.sort(),'Central equipment does not match wealth');
assert.equal(mayors.length,p.countryWealth>=2?1:0,'Mayor threshold or unique count is wrong');
const family={2:'lamp',3:'exterior',4:'interior'},solid=new Set();
for(const r of p.regions.filter(r=>['asset_plot','settlement_facility','castle_fixture','castle_portal'].includes(r.kind)))
 for(let y=r.y0;y<=r.y1;y++)for(let x=r.x0;x<=r.x1;x++){
  assert(!solid.has(y*p.width+x)||r.kind==='castle_portal','Central equipment overlaps a building/furnishing');solid.add(y*p.width+x);
 }
const distance=m=>Math.hypot(m.x-centre.x,m.y-centre.y);
for(const r of facilities){
 assert.equal(p.regions[r.parentIndex]?.kind,'village_center');assert.equal(r.parentId,centres[0].id);
 const model=db[family[r.objectType]]?.['#'+r.objectVariant+'#']?.cols;assert(model,'Unknown native civic model');
 assert.equal(r.x1-r.x0+1,r.rotation%2?model.d:model.w);assert.equal(r.y1-r.y0+1,r.rotation%2?model.w:model.d);assert.equal(r.objectHeight,model.h);
 const access=p.markers.find(m=>m.kind==='settlement_facility_access'&&m.id===r.id);assert(access&&distance(access)<=24,'Equipment is not near the centre');
 for(let y=r.y0-2;y<=r.y1+2;y++)for(let x=r.x0-2;x<=r.x1+2;x++){
  assert(x>=0&&y>=0&&x<p.width&&y<p.depth);if(p.heightOffsets.length)assert.equal(p.heightOffsets[y*p.width+x],0);
  if(p.shoreCuts.length)assert.equal(p.shoreCuts[y*p.width+x],0);
 }
}
for(const r of mayors){
 let k;for(let i=0;i<a.assetCount;i++)if(r.role==='MA'+String(a['asset'+i+'Id']).padStart(3,'0')&&r.rotation===a['asset'+i+'Rotation']){k='asset'+i;break;}
 assert(k&&a[k+'Residential']===1,'Mayor lives in a non-residential building');assert.equal(a[k+'DoorSide'],2,'Mayor entrance is not south-facing');
 assert(Math.max(a[k+'Width'],a[k+'Depth'])>12,'Mayor house is not large');
 const approach=p.markers.find(m=>m.kind==='asset_approach'&&m.regionIndex===p.regions.indexOf(r));
 assert(approach&&distance(approach)<=32,'Mayor residence is too far from the centre');
}
const floor=b=>[0,1,2,5,6,10,11,12,14].includes(b);
const pass=(x,y)=>x>=0&&y>=0&&x+1<p.width&&y+1<p.depth&&[0,1,p.width,p.width+1].every(k=>floor(p.biomes[y*p.width+x+k])&&!solid.has(y*p.width+x+k));
assert(pass(centre.x,centre.y));const seen=new Set([centre.y*p.width+centre.x]),pending=[...seen];
for(let k=0;k<pending.length;k++)for(const [dx,dy] of [[1,0],[-1,0],[0,1],[0,-1]]){
 const x=pending[k]%p.width+dx,y=Math.floor(pending[k]/p.width)+dy,j=y*p.width+x;
 if(!seen.has(j)&&pass(x,y)){seen.add(j);pending.push(j);}
}
for(const m of p.markers.filter(m=>['asset_approach','settlement_facility_access'].includes(m.kind)||m.role==='keep_public_entry'))
 assert(seen.has(m.y*p.width+m.x),'A house or facility cannot be reached with its solid objects present');
let actualObjects=null;
if(auditPath){
 const actual=JSON.parse(fs.readFileSync(auditPath,'utf8'));assert.equal(actual.seed,p.seed);assert.equal(actual.type,p.type);
 assert.equal(actual.finalWorldChecked,true);assert.equal(actual.facilities.length,facilities.length);assert.equal(actual.mayors.length,mayors.length);
 assert(Math.abs(actual.wealth-p.countryWealth)<0.00001);
 for(const r of facilities){const placed=actual.facilities.find(f=>f.role===r.role);assert(placed);
  assert.equal(placed.x,r.x0);assert.equal(placed.z,p.depth-1-r.y1);assert.equal(placed.width,r.x1-r.x0+1);assert.equal(placed.depth,r.y1-r.y0+1);
  assert.equal(placed.objectType,r.objectType);assert.equal(placed.objectVariant,r.objectVariant);assert.equal(placed.rotation,r.rotation);assert.equal(placed.height,r.objectHeight);
 }
 for(const r of mayors){const placed=actual.mayors.find(h=>h.asset===r.role);assert(placed);for(const k of ['x0','y0','x1','y1','rotation','doorX','doorY'])assert.equal(placed[k],r[k]);}
 actualObjects=actual.facilities.length;
}
const report={passed:true,seed:p.seed,type:p.type,wealth:p.countryWealth,facilities:facilities.map(r=>r.role),mayor:mayors.map(r=>({asset:r.role,width:r.x1-r.x0+1,depth:r.y1-r.y0+1,rotation:r.rotation})),reachableFacilities:facilities.length,actualObjects};
if(reportPath)fs.writeFileSync(reportPath,JSON.stringify(report,null,2));console.log(JSON.stringify(report));
