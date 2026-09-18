import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

const $ = (selector) => document.querySelector(selector);
const stage = $('#stage');
const canvas = $('#viewport');
const loading = $('#loading');
const scene = new THREE.Scene();
const camera = new THREE.OrthographicCamera(-80, 80, 80, -80, .1, 1000);
const renderer = new THREE.WebGLRenderer({canvas, alpha: true, antialias: false, preserveDrawingBuffer: true});
renderer.setPixelRatio(1);
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.setClearColor(0x000000, 0);
const controls = new OrbitControls(camera, canvas);
controls.enablePan = false;
controls.minZoom = .65;
controls.maxZoom = 2.2;
controls.enableDamping = true;
controls.dampingFactor = .09;
controls.autoRotateSpeed = 1;
const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
$('#animate').checked = !reducedMotion.matches;
let model, display, manifest;
let expression = 'idle', mode = 'pixel', pixelSize = 3;
let ready = false, toastTimer, previousTime = 0;
let modelBaseY = 0;
const textures = new Map();
const clock = new THREE.Clock();
const expressions = ['idle', 'happy', 'love', 'surprise', 'sleep', 'off'];

function toast(message) {
  $('#toast').textContent = message;
  $('#toast').classList.add('visible');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => $('#toast').classList.remove('visible'), 2400);
}

function resize() {
  const {width, height} = stage.getBoundingClientRect();
  const aspect = width / Math.max(height, 1);
  const halfHeight = Math.max(66, 67 / aspect);
  camera.left = -halfHeight * aspect;
  camera.right = halfHeight * aspect;
  camera.top = halfHeight;
  camera.bottom = -halfHeight;
  camera.updateProjectionMatrix();
  const divisor = mode === 'pixel' ? pixelSize : 1 / Math.min(devicePixelRatio, 2);
  renderer.setSize(Math.max(1, Math.floor(width / divisor)), Math.max(1, Math.floor(height / divisor)), false);
  canvas.style.imageRendering = mode === 'pixel' ? 'pixelated' : 'auto';
}

function setView(view) {
  controls.autoRotate = false;
  $('#rotate').checked = false;
  camera.zoom = 1;
  controls.target.set(0, 9, -2);
  const positions = {front: [0, 9, 220], three: [116, 68, 210], back: [-120, 67, -210]};
  camera.position.set(...positions[view]);
  camera.updateProjectionMatrix();
  controls.update();
  controls.saveState();
  document.querySelectorAll('[data-view]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.view === view)));
  $('#view-label').textContent = {front: '正面视角', three: '三分之四视角', back: '背面视角'}[view];
}

function setExpression(next) {
  expression = next;
  if (display) display.material.map = textures.get(next);
  document.querySelectorAll('[data-expression]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.expression === next)));
  $('#pet-status').textContent = {idle:'待机中',happy:'好心情',love:'喜欢你',surprise:'发现新事物',sleep:'困倦中',off:'已息屏'}[next];
}

function saveCanvas(output, filename) {
  output.toBlob(blob => {
    if (!blob) { toast('图片导出失败'); return; }
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url; link.download = filename; link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }, 'image/png');
}

// A separate render target keeps exports independent of viewport size and orbit state.
function renderSprite(exportCamera, size = 160) {
  const target = new THREE.WebGLRenderTarget(size, size, {minFilter: THREE.NearestFilter, magFilter: THREE.NearestFilter});
  target.texture.colorSpace = THREE.SRGBColorSpace;
  const pixels = new Uint8Array(size * size * 4);
  const previousTarget = renderer.getRenderTarget();
  const previousMap = display.material.map;
  const previousY = model.position.y;
  try {
    model.position.y = modelBaseY;
    display.material.map = textures.get(expression);
    renderer.setRenderTarget(target);
    renderer.clear();
    renderer.render(scene, exportCamera);
    renderer.readRenderTargetPixels(target, 0, 0, size, size, pixels);
  } finally {
    renderer.setRenderTarget(previousTarget);
    display.material.map = previousMap;
    model.position.y = previousY;
    target.dispose();
  }
  const output = document.createElement('canvas'); output.width = output.height = size;
  const ctx = output.getContext('2d');
  const data = ctx.createImageData(size, size);
  for (let y = 0; y < size; y++) data.data.set(pixels.subarray((size-1-y)*size*4, (size-y)*size*4), y*size*4);
  ctx.putImageData(data, 0, 0);
  return output;
}

function snapshot() {
  if (!ready) return;
  const exportCamera = camera.clone();
  exportCamera.left = -67; exportCamera.right = 67; exportCamera.top = 67; exportCamera.bottom = -67;
  exportCamera.zoom = 1; exportCamera.updateProjectionMatrix();
  const sprite = renderSprite(exportCamera);
  const output = document.createElement('canvas'); output.width = output.height = 640;
  const ctx = output.getContext('2d'); ctx.imageSmoothingEnabled = false;
  ctx.drawImage(sprite, 0, 0, 640, 640);
  saveCanvas(output, `pico-${expression}.png`);
  toast('已导出透明 PNG');
}

function exportSheet() {
  if (!ready) return;
  const current = expression;
  const exportCamera = new THREE.OrthographicCamera(-62, 62, 62, -62, .1, 1000);
  exportCamera.position.set(0, 10, 220); exportCamera.lookAt(0, 10, 0);
  const output = document.createElement('canvas'); output.width = expressions.length*160; output.height = 160;
  const ctx = output.getContext('2d');
  try {
    expressions.forEach((name, index) => {
      expression = name;
      ctx.drawImage(renderSprite(exportCamera), index*160, 0);
    });
  } finally { expression = current; }
  saveCanvas(output, 'pico-expressions-160.png');
  toast('已导出 6 帧表情精灵图');
}

document.querySelectorAll('[data-expression]').forEach(b => b.addEventListener('click', () => setExpression(b.dataset.expression)));
document.querySelectorAll('[data-view]').forEach(b => b.addEventListener('click', () => setView(b.dataset.view)));
document.querySelectorAll('[data-mode]').forEach(b => b.addEventListener('click', () => {
  mode = b.dataset.mode;
  document.querySelectorAll('[data-mode]').forEach(el => el.setAttribute('aria-pressed', String(el === b)));
  $('#pixel-size').disabled = mode !== 'pixel';
  resize();
}));
$('#pixel-size').addEventListener('input', e => {
  pixelSize = Number(e.target.value); $('#pixel-value').textContent = `${pixelSize} px`; resize();
});
$('#rotate').addEventListener('change', e => { controls.autoRotate = e.target.checked; });
$('#reset').addEventListener('click', () => setView('three'));
$('#snapshot').addEventListener('click', snapshot);
$('#export-png').addEventListener('click', snapshot);
$('#export-sheet').addEventListener('click', exportSheet);
controls.addEventListener('start', () => {
  document.querySelectorAll('[data-view]').forEach(b => b.setAttribute('aria-pressed', 'false'));
  $('#view-label').textContent = '自由视角';
});
reducedMotion.addEventListener('change', event => {
  if (event.matches) { $('#animate').checked = false; $('#rotate').checked = false; controls.autoRotate = false; }
});
new ResizeObserver(resize).observe(stage);
setView('three');
resize();

async function init() {
  try {
    const response = await fetch('model.json');
    if (!response.ok) throw new Error(`Manifest: ${response.status}`);
    manifest = await response.json();
    const loader = new THREE.TextureLoader();
    for (const [name, path] of Object.entries(manifest.expressions)) {
      const texture = await loader.loadAsync(path);
      texture.colorSpace = THREE.SRGBColorSpace;
      texture.magFilter = texture.minFilter = THREE.NearestFilter;
      texture.generateMipmaps = false; texture.flipY = false;
      textures.set(name, texture);
    }
    const gltf = await new GLTFLoader().loadAsync(manifest.model);
    model = gltf.scene;
    modelBaseY = manifest.previewOffsetY ?? 0;
    model.position.y = modelBaseY;
    display = model.getObjectByName(manifest.screenNode);
    if (!display) throw new Error('Screen node is missing');
    // Quantized reflection moves with the view while the face stays crisp and emissive.
    display.material.onBeforeCompile = shader => {
      shader.vertexShader = shader.vertexShader.replace('#include <common>', `#include <common>
        varying vec3 vGlassNormal;
        varying vec3 vGlassView;`);
      shader.vertexShader = shader.vertexShader.replace('#include <project_vertex>', `#include <project_vertex>
        vGlassNormal = normalize(normalMatrix * normal);
        vGlassView = -mvPosition.xyz;`);
      shader.fragmentShader = shader.fragmentShader.replace('#include <common>', `#include <common>
        varying vec3 vGlassNormal;
        varying vec3 vGlassView;`);
      shader.fragmentShader = shader.fragmentShader.replace('#include <opaque_fragment>', `
        vec3 glassNormal = normalize(vGlassNormal);
        vec3 glassView = normalize(vGlassView);
        float rim = pow(1.0 - abs(dot(glassNormal, glassView)), 2.0);
        float softbox = pow(max(0.0, dot(reflect(-glassView, glassNormal), normalize(vec3(-0.55, 0.8, 1.5)))), 22.0);
        float reflection = floor((rim * 0.09 + softbox * 0.055) * 96.0) / 96.0;
        outgoingLight += vec3(0.61, 0.8, 0.73) * reflection;
        #include <opaque_fragment>`);
    };
    display.material.customProgramCacheKey = () => 'pico-crt-glass-v1';
    display.material.needsUpdate = true;
    model.traverse(node => {
      if (node.isMesh && node.material.map) {
        node.material.map.magFilter = node.material.map.minFilter = THREE.NearestFilter;
        node.material.map.generateMipmaps = false;
      }
    });
    scene.add(model);
    setExpression(expression);
    ready = true;
    loading.hidden = true;
    document.body.dataset.ready = 'true';
  } catch (error) {
    console.error(error);
    loading.textContent = '模型加载失败，请刷新重试';
    toast('加载失败，请检查模型和贴图文件');
  }
}

function animate() {
  requestAnimationFrame(animate);
  const elapsed = clock.getElapsedTime();
  const delta = Math.min(elapsed - previousTime, .05); previousTime = elapsed;
  controls.update(delta);
  if (ready) {
    const moving = $('#animate').checked && expression !== 'off' && !reducedMotion.matches;
    model.position.y = modelBaseY + (moving ? Math.round(Math.sin(elapsed * 1.8) * .7 * 2) / 2 : 0);
    const blink = moving && expression === 'idle' && elapsed % 4.8 > 4.62;
    display.material.map = textures.get(blink ? 'blink' : expression);
  }
  renderer.render(scene, camera);
}
window.addEventListener('DOMContentLoaded', () => window.lucide?.createIcons());
window.lucide?.createIcons();
init();
animate();
