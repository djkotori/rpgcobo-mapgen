// Independent check of the exported design, live resource dimensions and placed world.
const fs=require('node:fs'),assert=require('node:assert/strict');
const [planPath,resourcePath,reportPath,auditPath]=process.argv.slice(2);
const p=JSON.parse(fs.readFileSync(planPath,'utf8')),db=JSON.parse(fs.readFileSync(resourcePath,'utf8'));
assert.equal(p.type,'castle');
const rooms=p.regions.filter(r=>r.kind==='room'),fixtures=p.regions.filter(r=>r.kind==='castle_fixture');
const doors=p.regions.filter(r=>r.kind==='castle_door'),yards=p.regions.filter(r=>r.kind==='castle_activity');
const details=p.regions.filter(r=>r.kind==='castle_detail');
const portals=p.regions.filter(r=>r.kind==='castle_portal'),clearances=p.regions.filter(r=>r.kind==='castle_clearance');
assert(rooms.length>=4&&fixtures.length>0);assert.equal(doors.length,rooms.length-1);assert.equal(yards.length,1);
const solid=new Set();
assert.equal(portals.length,doors.length*2,'Each interior door needs two integrated stone jambs');
for(const r of portals){
 const parent=p.regions[r.parentIndex];assert(parent?.kind==='castle_door'&&parent.id===r.parentId);
 assert.equal(r.objectHeight,parent.objectHeight+1);
 for(let y=r.y0;y<=r.y1;y++)for(let x=r.x0;x<=r.x1;x++)solid.add(y*p.width+x);
}
for(const r of p.regions.filter(r=>r.kind==='asset_plot'||r.kind==='castle_fixture'||r.kind==='settlement_facility')){
 for(let y=r.y0;y<=r.y1;y++)for(let x=r.x0;x<=r.x1;x++){
  assert(!solid.has(y*p.width+x),'Furniture overlaps another object/building');solid.add(y*p.width+x);
 }
}
const floor=b=>[0,1,2,5,6,10,11,12,14].includes(b);
const pass=(x,y)=>x>=0&&y>=0&&x+1<p.width&&y+1<p.depth&&[0,1,p.width,p.width+1].every(k=>floor(p.biomes[y*p.width+x+k])&&!solid.has(y*p.width+x+k));
const entry=p.markers.find(m=>m.role==='keep_public_entry');assert(entry&&pass(entry.x,entry.y));
const seen=new Set([entry.y*p.width+entry.x]),q=[...seen];
for(let k=0;k<q.length;k++){const i=q[k];for(const [dx,dy] of [[1,0],[-1,0],[0,1],[0,-1]]){
 const x=i%p.width+dx,y=Math.floor(i/p.width)+dy,j=y*p.width+x;
 if(!seen.has(j)&&pass(x,y)){seen.add(j);q.push(j);}
}}
for(const r of [...rooms,...yards]){
 let reachable=false;for(let y=r.y0+2;y<r.y1;y++)for(let x=r.x0+2;x<r.x1;x++)reachable||=seen.has(y*p.width+x);
 assert(reachable,'A furnished room/yard is unreachable: '+r.role);
}
// Independently check comfortable circulation, not just a narrow 2x2 escape.
let wideConnectedRooms=0;
for(const room of rooms){
 const wide=(x,y)=>x>=room.x0&&y>=room.y0&&x+3<=room.x1&&y+3<=room.y1&&
  Array.from({length:16},(_,i)=>[x+i%4,y+Math.floor(i/4)]).every(([xx,yy])=>floor(p.biomes[yy*p.width+xx])&&!solid.has(yy*p.width+xx));
 const landings=doors.filter(r=>r.parentId===room.id||r.linkedRegionId===room.id).map(r=>{
  const x=r.rotation%2?(r.x0<=room.x0+1?r.x1+1:r.x0-4):r.x0;
  const y=r.rotation%2?r.y0:(r.y0<=room.y0+1?r.y1+1:r.y0-4);
  assert(wide(x,y),'Door has no unobstructed 4x4 landing in '+room.role);return y*p.width+x;
 });
 const reached=new Set([landings[0]]),pending=[...reached];
 for(let k=0;k<pending.length;k++)for(const [dx,dy] of [[1,0],[-1,0],[0,1],[0,-1]]){
  const x=pending[k]%p.width+dx,y=Math.floor(pending[k]/p.width)+dy,j=y*p.width+x;
  if(!reached.has(j)&&wide(x,y)){reached.add(j);pending.push(j);}
 }
 assert(landings.every(i=>reached.has(i)),'Door-to-door circulation narrows below 4 blocks in '+room.role);wideConnectedRooms++;
}
const kinds={1:'door',2:'lamp',4:'interior'};
for(const r of [...fixtures,...doors,...details]){
 const parent=p.regions[r.parentIndex];assert(parent,'Missing semantic parent');assert.equal(parent.id,r.parentId);
 if(r.kind==='castle_fixture'){
  assert(r.x0>=parent.x0&&r.y0>=parent.y0&&r.x1<=parent.x1&&r.y1<=parent.y1,'Furniture escaped its room');
  let approach=false;
  for(let y=r.y0-1;y<=r.y1;y++)for(const x of [r.x0-2,r.x1+1])approach||=seen.has(y*p.width+x);
  for(let x=r.x0-1;x<=r.x1;x++)for(const y of [r.y0-2,r.y1+1])approach||=seen.has(y*p.width+x);
  assert(approach,'No 2x2 approach to '+r.role);
  if(parent.role.includes('council'))assert(!r.role.includes('bed'),'Council chamber was treated as a bedroom');
 }
 if(r.kind==='castle_detail')assert(parent.kind==='castle_fixture'&&r.x0>=parent.x0&&r.y0>=parent.y0&&r.x1<=parent.x1&&r.y1<=parent.y1,'Tabletop detail is outside its supporting furniture');
 if(r.objectType===-2){assert.equal(r.role,'training_dummy');assert.equal(r.x1-r.x0+1,4);assert.equal(r.y1-r.y0+1,2);continue;}
 const model=db[kinds[r.objectType]]?.['#'+r.objectVariant+'#'];assert(model?.cols,'No live native model for '+r.role);
 const width=r.rotation%2?model.cols.d:model.cols.w,depth=r.rotation%2?model.cols.w:model.cols.d;
 if(r.kind==='castle_door'){
  assert(width<=r.x1-r.x0+1&&depth<=r.y1-r.y0+1);
  assert(rooms.some(room=>room.id===r.linkedRegionId),'Door has no connected room');
  for(let y=r.y0;y<=r.y1;y++)for(let x=r.x0;x<=r.x1;x++)assert(floor(p.biomes[y*p.width+x]),'Door opening is masonry');
 }else{assert.equal(r.x1-r.x0+1,width,'Native width mismatch');assert.equal(r.y1-r.y0+1,depth,'Native depth mismatch');}
 assert.equal(r.objectHeight,model.cols.h);
}
const roles=Object.fromEntries(fixtures.map(r=>r.role).map(role=>[role,fixtures.filter(r=>r.role===role).length]));
for(const r of clearances)for(const f of fixtures){
 if(r.role==='throne_buffer'&&f.role==='royal_throne')continue;
 assert(f.x1<r.x0||f.x0>r.x1||f.y1<r.y0||f.y0>r.y1,'Furniture encroaches on '+r.role+': '+f.role);
}
const audience=clearances.filter(r=>r.role==='audience_space');assert.equal(audience.length,1);
assert(audience[0].x1-audience[0].x0+1>=10&&audience[0].y1-audience[0].y0+1>=8,'Audience space is too cramped');
const throne=fixtures.find(r=>r.role==='royal_throne'),throneRoom=p.regions[throne.parentIndex];
const throneModel=db[kinds[throne.objectType]]['#'+throne.objectVariant+'#'].cols;
assert.equal(throne.x0,Math.floor((throneRoom.x0+throneRoom.x1)/2)-Math.floor((throneModel.w-1)/2),'Throne slipped away from the ceremonial axis');
assert(throne.y0>=throneRoom.y0+3&&audience[0].y0===throne.y1+1);
assert(doors.some(r=>r.objectVariant===13),'Ceremonial rooms use the same plain utility door');
assert.equal(roles.royal_throne,1);assert(roles.royal_bed||roles.garrison_bed);assert(roles.cooking_stove);assert(roles.training_dummy);
const expected={concentric:'banquet_table',hill:'command_table',river:'customs_counter',palace:'ballroom_piano',fortress:'command_table'};
const militaryHall=p.archetype==='hill'||p.archetype==='fortress';
assert(roles[expected[p.archetype]]||(militaryHall&&roles.garrison_mess_table),'Hall function ignores castle type');
const dance=p.regions.filter(r=>r.kind==='castle_surface'&&r.role==='ballroom_floor');
assert.equal(dance.length,p.archetype==='palace'?1:0);
for(const r of dance)for(let y=r.y0;y<=r.y1;y++)for(let x=r.x0;x<=r.x1;x++)assert(!solid.has(y*p.width+x),'Dance floor obstructed');
let actualObjects=null;
if(auditPath){
 const actual=JSON.parse(fs.readFileSync(auditPath,'utf8'));assert.equal(actual.seed,p.seed);assert.equal(actual.castleType,p.archetype);
 assert.equal(actual.finalWorldChecked,true,'Only intermediate placements were checked');
 assert.equal(actual.fixtures.length,fixtures.length);assert.equal(actual.doors.length,doors.length);
 assert.equal(actual.details.length,details.length);
 assert.equal(actual.portals.length,portals.length);assert.equal(actual.clearances.length,clearances.length);
 const remaining=[...actual.fixtures,...actual.doors,...actual.details];
 const groundY=actual.doors[0]?.y??actual.fixtures.find(r=>r.objectType>=0)?.y;
 for(const r of portals){
  assert(actual.portals.some(a=>a.parentIndex===r.parentIndex&&a.x===r.x0&&a.z===p.depth-1-r.y1&&
   a.y===groundY&&a.height===r.objectHeight&&a.width===r.x1-r.x0+1&&a.depth===r.y1-r.y0+1),'Stone door frame missing from the actual world');
 }
 for(const r of [...fixtures,...doors,...details]){
  const i=remaining.findIndex(a=>a.role===r.role&&a.parentIndex===r.parentIndex&&a.x===r.x0&&a.z===p.depth-1-r.y1&&a.rotation===r.rotation);
  assert(i>=0,'Native object missing from placed world: '+r.role);const a=remaining.splice(i,1)[0];
  assert.equal(a.objectType,r.objectType);assert.equal(a.objectVariant,r.objectVariant);assert.equal(a.height,r.objectHeight);
  const model=r.objectType<0?{w:4,d:2}:db[kinds[r.objectType]]['#'+r.objectVariant+'#'].cols;
  assert.equal(a.width,r.rotation%2?model.d:model.w);assert.equal(a.depth,r.rotation%2?model.w:model.d);
  if(r.kind==='castle_detail')assert.equal(a.y,groundY+p.regions[r.parentIndex].objectHeight,'Detail is floating above or inside its supporting table');
 }
 actualObjects=actual.fixtures.length+actual.doors.length+actual.details.length;
}
const report={passed:true,seed:p.seed,type:p.archetype,rooms:rooms.length,doors:doors.length,fixtures:fixtures.length,details:details.length,roles,trainingYards:yards.length,clearDanceFloors:dance.length,
 audienceSize:[audience[0].x1-audience[0].x0+1,audience[0].y1-audience[0].y0+1],wideConnectedRooms,reservedCirculation:clearances.filter(r=>r.role==='circulation').length,stoneDoorJambs:portals.length,actualObjects};
if(reportPath)fs.writeFileSync(reportPath,JSON.stringify(report,null,2));console.log(JSON.stringify(report));
