/**
 * Builds the plugin and the view and deploys both into an MO2 mod folder.
 *
 * Usage:
 *   node deploy.mjs                  # target comes from .env in the project root
 *   node deploy.mjs "D:\...\my-damage"  # overrides the .env target
 *   node deploy.mjs --no-build      # deploy the existing artifacts as they are
 *
 * The mod folder ends up with this layout:
 *   <target>/PrismaUI/views/PrismaUI-Example-UI/   <- view/dist
 *   <target>/SKSE/Plugins/<plugin>.dll              <- the xmake build output
 */
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { cp, mkdir, readFile, readdir, rm, stat } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const VIEW_FOLDER = 'PrismaUI-Example-UI';

const projectDir = dirname(fileURLToPath(import.meta.url));
const viewDir = join(projectDir, 'view');
const distDir = join(viewDir, 'dist');
const buildDir = join(projectDir, 'build');
const envFile = join(projectDir, '.env');
const xmakeFile = join(projectDir, 'xmake.lua');

const args = process.argv.slice(2);
const skipBuild = args.includes('--no-build');
const targetArg = args.find((arg) => !arg.startsWith('--'));

const isWindows = process.platform === 'win32';

/**
 * Reads a KEY=VALUE style file. Windows paths keep their backslashes, so escape
 * sequences are deliberately not interpreted.
 */
async function readEnvFile(path) {
  let text;
  try {
    text = await readFile(path, 'utf8');
  } catch {
    return {};
  }

  const values = {};
  for (const rawLine of text.split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line || line.startsWith('#')) continue;

    const match = line.match(/^(?:export\s+)?([\w.-]+)\s*=\s*(.*)$/);
    if (!match) continue;

    const key = match[1];
    let value = match[2].trim();
    if (
      (value.startsWith('"') && value.endsWith('"') && value.length > 1) ||
      (value.startsWith("'") && value.endsWith("'") && value.length > 1)
    ) {
      value = value.slice(1, -1);
    }

    values[key] = value;
  }
  return values;
}

/** Runs a command in the given folder and rejects when it fails. */
function run(command, commandArgs, cwd) {
  return new Promise((resolvePromise, reject) => {
    // The commonlibsse-ng rule installs the plugin into the mods folder of its
    // own accord when one of these is set. The deployment is done here, so they
    // are hidden from the build to keep a single target folder.
    const env = { ...process.env };
    delete env.XSE_TES5_GAME_PATH;
    delete env.XSE_TES5_MODS_PATH;

    const child = spawn(command, commandArgs, {
      cwd,
      stdio: 'inherit',
      shell: false,
      env,
    });

    child.on('error', reject);
    child.on('close', (code) => {
      if (code === 0) {
        resolvePromise();
      } else {
        reject(new Error(`${command} exited with code ${code}`));
      }
    });
  });
}

/**
 * Builds the npm command. Recent Node versions refuse to spawn a .cmd file
 * without a shell, so npm's entry script is run by node itself.
 */
function resolveNpmCommand() {
  const npmCli = join(dirname(process.execPath), 'node_modules', 'npm', 'bin', 'npm-cli.js');

  try {
    if (existsSync(npmCli)) {
      return { command: process.execPath, args: [npmCli] };
    }
  } catch {
    // fall through to the plain npm command
  }

  return { command: isWindows ? 'npm.cmd' : 'npm', args: [] };
}

/** Reads the plugin target name from xmake.lua. */
async function readPluginName() {
  const text = await readFile(xmakeFile, 'utf8');
  const match = text.match(/^\s*target\s*\(\s*["']([^"']+)["']/m);
  return match ? match[1] : null;
}

/** Finds the built plugin dll, newest build wins. */
async function findPluginDll(name) {
  const found = [];

  async function walk(dir, depth) {
    if (depth > 6) return;

    let entries;
    try {
      entries = await readdir(dir, { withFileTypes: true });
    } catch {
      return;
    }

    for (const entry of entries) {
      const full = join(dir, entry.name);
      if (entry.isDirectory()) {
        await walk(full, depth + 1);
      } else if (entry.isFile() && entry.name.toLowerCase() === `${name}.dll`.toLowerCase()) {
        const info = await stat(full);
        found.push({ path: full, mtime: info.mtimeMs });
      }
    }
  }

  await walk(buildDir, 0);
  if (found.length === 0) return null;

  found.sort((a, b) => b.mtime - a.mtime);
  return found[0].path;
}

/** Replaces the contents of a folder with the given source folder. */
async function replaceFolder(source, target) {
  await mkdir(target, { recursive: true });

  for (const entry of await readdir(target)) {
    await rm(join(target, entry), { recursive: true, force: true });
  }

  await cp(source, target, { recursive: true });
}

async function main() {
  const env = await readEnvFile(envFile);
  const target = targetArg ?? env.SKYRIM_MOD_PATH;

  if (!target) {
    console.error(
      'No target mod folder given.\n' +
        `Add SKYRIM_MOD_PATH to "${envFile}" or pass the folder as an argument.`,
    );
    process.exit(1);
  }

  const targetDir = resolve(target);

  try {
    const info = await stat(targetDir);
    if (!info.isDirectory()) throw new Error('not a directory');
  } catch {
    console.error(`The mod folder "${targetDir}" does not exist.`);
    process.exit(1);
  }

  if (!skipBuild) {
    console.log('Building the plugin with xmake...');
    await run('xmake', ['build'], projectDir);

    console.log('Building the view...');
    const npm = resolveNpmCommand();
    await run(npm.command, [...npm.args, 'run', 'build'], viewDir);
  }

  const pluginName = await readPluginName();
  if (!pluginName) {
    console.error(`Could not read the plugin target name from "${xmakeFile}".`);
    process.exit(1);
  }

  const pluginDll = await findPluginDll(pluginName);
  if (!pluginDll) {
    console.error(
      `Could not find "${pluginName}.dll" in "${buildDir}". Run "xmake build" first.`,
    );
    process.exit(1);
  }

  try {
    await stat(distDir);
  } catch {
    console.error(`"${distDir}" does not exist. Run "npm run build" in "view" first.`);
    process.exit(1);
  }

  const viewTarget = join(targetDir, 'PrismaUI', 'views', VIEW_FOLDER);
  const pluginTarget = join(targetDir, 'SKSE', 'Plugins');

  await replaceFolder(distDir, viewTarget);
  console.log(`View deployed to ${viewTarget}`);

  await mkdir(pluginTarget, { recursive: true });
  await cp(pluginDll, join(pluginTarget, `${pluginName}.dll`), { force: true });
  console.log(`Plugin deployed to ${join(pluginTarget, `${pluginName}.dll`)}`);
}

try {
  await main();
} catch (error) {
  console.error(error instanceof Error ? error.message : error);
  process.exit(1);
}
