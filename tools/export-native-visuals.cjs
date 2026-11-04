// Development-only extraction. Windows embeds this original title JPEG as RCDATA.
const fs=require('fs'),path=require('path');
const root=path.join(__dirname,'..');
const source=fs.readFileSync(path.join(root,'Spaceship.hta'),'utf8');
const match=source.match(/#title-card[^\n]+data:image\/jpeg;base64,([^"\)]+)/);
if(!match) throw Error('Reference title artwork not found');
const file=path.join(root,'cpp/assets/title.jpg'),image=Buffer.from(match[1],'base64');
if(process.argv.includes('--check')) {if(!fs.readFileSync(file).equals(image)) throw Error('Regenerate native title artwork');}
else fs.writeFileSync(file,image);
console.log('Native title image matches the original embedded HTA artwork');
