import * as anthropic from "@anthropic-ai/sdk";
import * as gemini from "@google/genai";
import * as openai from "openai";
import { loadSdksWith } from "./providers/sdks";
import { start } from "./start";

loadSdksWith((name) => ({ anthropic, gemini, openai })[name]);
start();
