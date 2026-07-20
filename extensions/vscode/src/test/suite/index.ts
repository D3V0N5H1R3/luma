import * as path from "node:path";
import Mocha from "mocha";
import { glob } from "glob";

export async function run(): Promise<void> {
    const mocha = new Mocha({ ui: "tdd", color: true, timeout: 10000 });
    const test_root = path.resolve(__dirname, ".");

    const files = await glob("**/**.test.js", { cwd: test_root });
    for (const f of files) {
        mocha.addFile(path.resolve(test_root, f));
    }

    return new Promise<void>((resolve, reject) => {
        mocha.run((failures) => {
            if (failures > 0) {
                reject(new Error(`${failures} test(s) failed.`));
            } else {
                resolve();
            }
        });
    });
}
