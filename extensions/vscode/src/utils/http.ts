// ─── HTTP utilities ───────────────────────────────────────────────

import * as fs from "node:fs";
import * as https from "node:https";
import * as http from "node:http";

const MAX_REDIRECTS = 5;
const DEFAULT_TIMEOUT_MS = 30_000;
const USER_AGENT = "luma-vscode-extension";

function sleep(ms: number): Promise<void> {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

const REDIRECT_STATUS_CODES = new Set([301, 302, 307, 308]);

/** Returns true if the HTTP status code is a redirect (301, 302, 307, 308). */
function isRedirect(status: number | undefined): boolean {
    return REDIRECT_STATUS_CODES.has(status ?? 0);
}

// ─── Retry policy ─────────────────────────────────────────────────

export interface RetryOptions {
    maxRetries?: number;
    baseDelay?: number;
    maxDelay?: number;
    context?: string;
}

const DEFAULT_RETRY_OPTIONS: Required<Omit<RetryOptions, "context">> = {
    maxRetries: 3,
    baseDelay: 1_000,
    maxDelay: 30_000,
};

/**
 * Retries `fn` with exponential backoff and jitter.
 * Throws the last error if all attempts fail.
 */
export async function withRetry<T>(fn: () => Promise<T>, options?: RetryOptions): Promise<T> {
    const { maxRetries, baseDelay, maxDelay } = { ...DEFAULT_RETRY_OPTIONS, ...options };
    let lastError: unknown;
    for (let attempt = 0; attempt <= maxRetries; attempt++) {
        try {
            return await fn();
        } catch (err) {
            lastError = err;
            if (attempt < maxRetries) {
                const exponential = Math.min(baseDelay * Math.pow(2, attempt), maxDelay);
                const jitter = exponential * (0.5 + 0.5 * Math.random());
                await sleep(jitter);
            }
        }
    }
    const baseMessage = lastError instanceof Error ? lastError.message : String(lastError);
    const context = options?.context ? ` to ${options.context}` : "";
    throw new Error(
        `HTTP request${context} failed after ${maxRetries + 1} attempts: ${baseMessage}`,
    );
}

/**
 * Follow redirects and return the raw HTTP response, rejecting on errors,
 * excessive redirects, HTTPS→HTTP downgrade attempts, or timeout.
 */
export function fetchResponse(url: string, redirects = 0): Promise<http.IncomingMessage> {
    return new Promise((resolve, reject) => {
        if (!url.startsWith("https://")) {
            reject(new Error(`Refusing non-HTTPS request to ${url}`));
            return;
        }
        if (redirects > MAX_REDIRECTS) {
            reject(new Error(`Too many redirects (>${MAX_REDIRECTS}) for ${url}`));
            return;
        }
        const req = https
            .get(url, { headers: { "User-Agent": USER_AGENT } }, (res) => {
                if (isRedirect(res.statusCode)) {
                    if (!res.headers.location) {
                        reject(
                            new Error(
                                `Redirect (HTTP ${res.statusCode}) without Location header for ${url}`,
                            ),
                        );
                        return;
                    }
                    if (url.startsWith("https") && !res.headers.location.startsWith("https")) {
                        reject(new Error("Refusing HTTPS → HTTP redirect (security downgrade)"));
                        return;
                    }
                    res.resume();
                    fetchResponse(res.headers.location, redirects + 1).then(resolve, reject);
                    return;
                }
                if (res.statusCode !== 200) {
                    reject(new Error(`HTTP ${res.statusCode} for ${url}`));
                    return;
                }
                resolve(res);
            })
            .on("error", reject);

        req.setTimeout(DEFAULT_TIMEOUT_MS, () => {
            req.destroy(new Error(`Request timed out after ${DEFAULT_TIMEOUT_MS}ms for ${url}`));
        });
    });
}

/** Collects the full response body into a Buffer. */
export function collectBody(res: http.IncomingMessage): Promise<Buffer> {
    return new Promise((resolve, reject) => {
        const chunks: Buffer[] = [];
        res.on("data", (chunk: Buffer) => chunks.push(chunk));
        res.on("end", () => resolve(Buffer.concat(chunks)));
        res.on("error", reject);
    });
}

/**
 * Fetches a URL, collects the full response body, and maps it through
 * `transform`. Retries on failure. Shared implementation behind fetchJson and
 * fetchText.
 */
async function fetchBody<T>(url: string, transform: (body: Buffer) => T): Promise<T> {
    return withRetry(
        async () => {
            const res = await fetchResponse(url);
            const body = await collectBody(res);
            return transform(body);
        },
        { context: url },
    );
}

/** Fetches a URL and parses the response body as JSON. Retries on failure. */
export async function fetchJson(url: string): Promise<unknown> {
    return fetchBody(url, (body) => JSON.parse(body.toString()));
}

/** Fetches a URL and returns the response body as a string. Retries on failure. */
export async function fetchText(url: string): Promise<string> {
    return fetchBody(url, (body) => body.toString());
}

/** Downloads a file from the given URL to the destination path. Retries on failure. */
export async function downloadFile(url: string, dest: string): Promise<void> {
    return withRetry(
        async () => {
            const res = await fetchResponse(url);
            return new Promise<void>((resolve, reject) => {
                const file = fs.createWriteStream(dest);
                res.pipe(file);
                // Remove the partial file and reject; shared by the response and
                // file-stream error handlers so both recover identically.
                const cleanup = (err: Error): void => {
                    file.destroy();
                    try {
                        fs.unlinkSync(dest);
                    } catch {
                        /* cleanup best-effort */
                    }
                    reject(err);
                };
                res.on("error", cleanup);
                file.on("finish", () => {
                    file.close();
                    resolve();
                });
                file.on("error", cleanup);
            });
        },
        { context: url },
    );
}
