import {readFile, writeFile} from 'node:fs/promises';
import {validateBytes} from 'gltf-validator';

const bytes = await readFile(new URL('./tv_bot.glb', import.meta.url));
const report = await validateBytes(new Uint8Array(bytes), {uri: 'tv_bot.glb', maxIssues: 1000});
await writeFile(new URL('./validation-gltf.json', import.meta.url), JSON.stringify(report, null, 2));
console.log(JSON.stringify({...report.issues, messages: report.issues.messages.slice(0, 8)}, null, 2));
if (report.issues.numErrors || report.issues.numWarnings) process.exitCode = 1;
