import { createServerFn } from "@tanstack/react-start";
import { fillNames, MODE_PROMPTS, OFFLINE_LINES } from "./personalities";
import type { RobotMode } from "./types";

type ChatInput = {
  message: string;
  kidName: string;
  daddyName: string;
  mode: RobotMode;
  history: { role: "user" | "assistant"; text: string }[];
};

function pickOffline(mode: RobotMode, kid: string, daddy: string) {
  const lines = OFFLINE_LINES[mode];
  const line = lines[Math.floor(Math.random() * lines.length)] ?? lines[0];
  return fillNames(line, kid, daddy);
}

function limitWords(text: string, max = 18) {
  const words = text.trim().split(/\s+/);
  return words.length <= max ? text.trim() : words.slice(0, max).join(" ");
}

export const chatWithBuddy = createServerFn({ method: "POST" })
  .validator((input: ChatInput) => {
    const message = (input.message ?? "").trim().slice(0, 280);
    const kidName = (input.kidName ?? "AJ").trim().slice(0, 24) || "AJ";
    const daddyName = (input.daddyName ?? "Daddy").trim().slice(0, 24) || "Daddy";
    const mode: RobotMode = ["normal", "dog", "bodyguard", "party"].includes(input.mode)
      ? input.mode
      : "normal";
    const history = (input.history ?? []).slice(-6).map((m) => ({
      role: m.role === "assistant" ? ("assistant" as const) : ("user" as const),
      text: String(m.text ?? "").slice(0, 280),
    }));
    return { message, kidName, daddyName, mode, history };
  })
  .handler(async ({ data }) => {
    if (!data.message) {
      return { ok: false as const, error: "Say something first" };
    }

    const apiKey = process.env.XAI_API_KEY;
    if (!apiKey) {
      return {
        ok: true as const,
        text: pickOffline(data.mode, data.kidName, data.daddyName),
        provider: "offline" as const,
      };
    }

    const system = fillNames(MODE_PROMPTS[data.mode], data.kidName, data.daddyName);
    const messages = [
      { role: "system", content: system },
      ...data.history.map((m) => ({
        role: m.role,
        content: m.text,
      })),
      { role: "user", content: data.message },
    ];

    try {
      const res = await fetch("https://api.x.ai/v1/chat/completions", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${apiKey}`,
        },
        body: JSON.stringify({
          model: "grok-4.5",
          max_tokens: 80,
          temperature: 0.7,
          messages,
        }),
      });
      if (!res.ok) {
        return {
          ok: true as const,
          text: pickOffline(data.mode, data.kidName, data.daddyName),
          provider: "offline" as const,
        };
      }
      const body = (await res.json()) as {
        choices?: { message?: { content?: string } }[];
      };
      const text = limitWords(body.choices?.[0]?.message?.content ?? "");
      return {
        ok: true as const,
        text: text || pickOffline(data.mode, data.kidName, data.daddyName),
        provider: "grok" as const,
      };
    } catch {
      return {
        ok: true as const,
        text: pickOffline(data.mode, data.kidName, data.daddyName),
        provider: "offline" as const,
      };
    }
  });
