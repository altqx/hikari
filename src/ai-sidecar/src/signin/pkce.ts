const base64url = (bytes: Uint8Array) => Buffer.from(bytes).toString("base64url");

export const randomToken = (bytes = 32) => base64url(crypto.getRandomValues(new Uint8Array(bytes)));

export async function challengeOf(verifier: string): Promise<string> {
    return base64url(new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(verifier))));
}

/** A JWT's claims, unverified: the token came straight from the issuer over TLS. */
export function claimsOf(token: string): Record<string, unknown> {
    const payload = token.split(".")[1];
    if (!payload)
        return {};
    try {
        const claims: unknown = JSON.parse(Buffer.from(payload, "base64url").toString("utf8"));
        return claims && typeof claims === "object" ? (claims as Record<string, unknown>) : {};
    } catch {
        return {};
    }
}
