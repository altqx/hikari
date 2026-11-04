export type JsonSchema = Record<string, unknown>;

/**
 * `schema` with every `#/$defs/` reference replaced by its definition, `const` written as a one-value `enum`
 * (Gemini takes no `const`), and each description passed through `describe`. A sibling of a `$ref` wins over the
 * definition's own keyword, so a property keeps its own description.
 */
export function inline(schema: JsonSchema, defs: Record<string, JsonSchema>, describe: (text: string) => string,
    seen: readonly string[] = []): JsonSchema {
    if (typeof schema.$ref === "string") {
        const name = schema.$ref.replace("#/$defs/", "");
        const def = defs[name];
        if (!def || seen.includes(name))
            throw new Error(`HA1's schema reference ${schema.$ref} doesn't resolve`);
        const { $ref: _, ...rest } = schema;
        return inline({ ...def, ...rest }, defs, describe, [...seen, name]);
    }
    const child = (value: unknown) => inline(value as JsonSchema, defs, describe, seen);
    const out: JsonSchema = {};
    for (const [key, value] of Object.entries(schema)) {
        if (key === "const")
            out.enum = [value];
        else if (key === "description" && typeof value === "string")
            out.description = describe(value);
        else if (key === "properties")
            out.properties = Object.fromEntries(Object.entries(value as Record<string, unknown>)
                .map(([name, property]) => [name, child(property)]));
        else if (key === "items")
            out.items = child(value);
        else if (key === "oneOf" || key === "anyOf")
            out[key] = (value as unknown[]).map(child);
        else
            out[key] = value;
    }
    return out;
}
