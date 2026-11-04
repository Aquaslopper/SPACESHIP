// Development only: preserve the reference game's self-generated offline audio.
const fs=require('fs'),path=require('path'),vm=require('vm');
const root=path.join(__dirname,'..'),g={document:{getElementById:()=>null},window:{location:{protocol:'http:'}}};
vm.createContext(g);
vm.runInContext(fs.readFileSync(path.join(root,'Spaceship.hta'),'utf8').match(/<script type="text\/javascript">([\s\S]*?)<\/script>/)[1],g);
for(const [kind,relative] of [['music','music/ambient.wav'],['fx-tap','sounds/click.wav'],['fx-ping','sounds/scan.wav'],['fx-thrust','sounds/travel.wav'],['fx-chime','sounds/complete.wav']]) {
 const file=path.join(root,'runtime',relative),data=Buffer.from(g.waveDataUri(kind).split(',')[1],'base64');
 if(process.argv.includes('--check')) {if(!fs.readFileSync(file).equals(data)) throw Error('Regenerate '+relative);}
 else {fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,data);}
}
console.log('Native audio: ambient loop and four reference-synthesized WAV cues');
