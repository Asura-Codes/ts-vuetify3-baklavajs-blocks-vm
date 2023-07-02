import * as nearley from 'nearley'
import compiler from '../compiler/compiler'
import * as fs from 'fs'
import { createInterface } from 'readline'


interface FileOffset {
    fd: number;
    offset: number;
}

interface CompilerFile {
    d: FileOffset;
    writeCmd: (cmd: number) => void;
    writeShort: (short: number) => void;
    writeString: (str: string) => void;
}

function createCompilerFile(file: string): CompilerFile {
    const d = {
        fd: fs.openSync(file, "w+"),
        offset: 0
    } as FileOffset;

    const writeCmd = (cmd: number) => {
        fs.writeSync(d.fd, Buffer.from([(cmd & 0xFF)]), 0, 1, d.offset);
        d.offset += 1;
    }
    const writeShort = (short: number) => {
        fs.writeSync(d.fd, Buffer.from([short & 0xFF, (short / 256) & 0xFF]), 0, 2, d.offset);
        d.offset += 2;
    }
    const writeString = (str: string) => {
        fs.writeSync(d.fd, Buffer.from([str.length & 0xFF, (str.length / 256) & 0xFF]), 0, 2, d.offset);
        d.offset += 2;
        fs.writeSync(d.fd, Buffer.from(str), 0, str.length, d.offset);
        d.offset += str.length;
    }

    return {
        d,
        writeCmd,
        writeShort,
        writeString
    }
}


// Line by line
async function compileFromFile(file: string) {
    try {
        const rl = createInterface({
            input: fs.createReadStream(file), //or fileStream 
            crlfDelay: Infinity
        });

        const parser = new nearley.Parser(
            nearley.Grammar.fromCompiled(compiler),
            { keepHistory: true }
        );

        for await (const line of rl) {
            console.log(line)
            parser.feed(line.trim() + "\n");
        }

        if (parser.results.length) {
            const results = parser.results[0].filter(e => e !== null);

            const output = file.replace(/\.[^.]+$/, '') + '.raw';
            const out = createCompilerFile(output);

            const LABELS = new Map<string, number>();
            const GOTOS = new Map<number, string>();
            results.forEach(element => {
                if (element.length) {
                    const cmd = element[0];
                    const rest = element.slice(1);
                    if (cmd.length) { // string
                        // Label
                        LABELS.set(rest[0].label, out.d.offset);
                    } else {
                        if (cmd >= 0x10 && cmd <= 0x12) {
                            // Jump
                            out.writeCmd(cmd);
                            out.writeShort(0);
                        } else {
                            // Command
                            out.writeCmd(cmd);

                            // Data and registers
                            rest.forEach(e => {
                                if (e.reg) {
                                    out.writeCmd(e.reg);
                                } else if (e.label) {
                                    GOTOS.set(out.d.offset, e.label);
                                    out.writeShort(0);
                                } else if (e.length) {
                                    out.writeString(e);
                                } else {
                                    out.writeShort(e);
                                }
                            });
                        }
                    }
                }
            });

            GOTOS.forEach((value, key) => {
                const offset = LABELS.get(value);
                if (offset) {
                    console.log(`${key}: ${value} [${offset}]`);
                    out.d.offset = key;
                    out.writeShort(offset);
                } else {
                    console.log(`Brak punktu skoku: ${value}`);
                    throw `Undefined label: ${value}`;
                }
            });

            console.log(results);
        }
        console.log('Done');
    } catch (err) {
        console.error(err);
        throw "";
    }
};

async function test() {
    try {
        await compileFromFile('examples/add.in');
        await compileFromFile('examples/call.in');
        await compileFromFile('examples/compare.in');
        await compileFromFile('examples/concat.in');
        await compileFromFile('examples/dec.in');
        await compileFromFile('examples/equal.in');
        await compileFromFile('examples/jump.in');
        await compileFromFile('examples/loop.in');
        await compileFromFile('examples/memcpy.in');
        await compileFromFile('examples/mul.in');
        await compileFromFile('examples/poke.in');
        await compileFromFile('examples/random.in');
        await compileFromFile('examples/simple.in');
        await compileFromFile('examples/stack.in');
        await compileFromFile('examples/system.in');
        await compileFromFile('examples/types.in');
    } catch (err) {
        console.error(err);
    }
}

test();
