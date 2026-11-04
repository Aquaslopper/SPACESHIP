// Extract the HTA's actual box-of-divs illustrations, not replacement drawings.
// DOM construction happens only here at development time. Runtime is native C++.
module.exports=function extract(g,source) {
 const rules=[];
 const css=source.match(/<style[^>]*>([\s\S]*?)<\/style>/)[1].replace(/\/\*[\s\S]*?\*\//g,'');
 for(const m of css.matchAll(/([^{}]+)\{([^{}]*)\}/g)) {
  const declarations={};for(const d of m[2].split(';')) {const i=d.indexOf(':');if(i>=0) declarations[d.slice(0,i).trim()]=d.slice(i+1).trim();}
  for(const selector of m[1].split(',')) if(/^[.#][\w-]+$/.test(selector.trim())) rules.push({selector:selector.trim(),declarations});
 }
 const nodes={};
 class Element {
  constructor(cls='',html='') {this.className=cls||'';this.style={};this.children=[];this.innerHTML=html||'';}
  set id(v){this._id=v;nodes[v]=this;}get id(){return this._id||'';}
  appendChild(c){this.children.push(c);return c;}removeChild(c){this.children.splice(this.children.indexOf(c),1);}
  get firstChild(){return this.children[0]||null;}
 }
 const make=g.make,doc=g.document.getElementById;
 g.make=(tag,cls,html)=>new Element(cls,html);g.document.getElementById=id=>nodes[id]||null;
 function computed(node) {
  const style={};const classes=node.className.split(/\s+/);
  for(const r of rules) if(r.selector[0]==='.' && classes.includes(r.selector.slice(1))) Object.assign(style,r.declarations);
  for(const r of rules) if(r.selector==='#'+node.id) Object.assign(style,r.declarations);
  Object.assign(style,node.style);return style;
 }
 const text=s=>String(s).replace(/<[^>]*>/g,' ').replace(/&nbsp;/g,' ').replace(/&amp;/g,'&').trim();
 function flatten(node,ox=0,oy=0,out=[]) {
  const st=computed(node),x=ox+(parseFloat(st.left)||0),y=oy+(parseFloat(st.top)||0);
  if(st.display==='none') return out;
  if(node!==out.root && (st.position==='absolute' || node.id)) {
   out.push({id:node.id,classes:node.className,x,y,w:parseFloat(st.width)||0,h:parseFloat(st.height)||0,style:st,text:text(node.innerHTML),
    runs:[...String(node.innerHTML).matchAll(/<span class="([^"]+)">([^<]*)<\/span>/g)].map(m=>({text:text(m[2]),style:computed(new Element(m[1]))}))});
  }
  for(const child of node.children) flatten(child,x,y,out);return out;
 }
 function scene(build) {const root=build(),out=[];out.root=root;flatten(root,0,0,out);return {w:parseFloat(root.style.width),h:parseFloat(root.style.height),parts:Array.from(out)};}
 const result={engine:scene(()=>g.buildMachine()),o2:scene(()=>g.buildFilterMachine()),mess:scene(()=>g.buildMessMachine()),quarters:scene(()=>g.buildHallway()),pixelSize:g.PX,figures:{}};
 // Call the real sprite painter, including its authored props, poses and hats.
 for(const person of [...g.CREW,...g.CANDIDATES]) {
  result.figures[person.id]={};
  for(const mode of ['default','rest','doc','study',...g.JOBS.map(j=>'job:'+j.id)]) {
   g.STATE.pendingOrders[person.id]=mode==='rest'?{rest:true}:mode==='doc'?{doc:true}:mode==='study'?{learnOn:true}:mode.startsWith('job:')?{job:mode.slice(4)}:{};
   g.FIGURE_SIG='';g.paintSprite(person);
   result.figures[person.id][mode]=flatten(nodes['crew-scene'],0,0,[]).filter(p=>p.classes==='cq-px').map(p=>[p.x,p.y,p.style.background]);
  }
  delete g.STATE.pendingOrders[person.id];
 }
 g.make=make;g.document.getElementById=doc;
 return result;
};
