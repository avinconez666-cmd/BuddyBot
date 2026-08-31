export type RobotMode = "normal" | "dog" | "bodyguard" | "party";

export type FaceState = "idle" | "listening" | "speaking" | "surprised" | "alert";

export type GameId = "jump" | "maze" | "starship" | "memory" | "color" | "math";

export type ChatRole = "user" | "assistant";

export type ChatMessage = {
  id: string;
  role: ChatRole;
  text: string;
};

export type Telemetry = {
  tempC: number;
  humidity: number;
  gas: number;
  flame: boolean;
  pir: boolean;
  tilt: boolean;
  us: { front: number; rear: number; left: number; right: number };
  heading: number;
  battery: number;
  gps: { lat: number; lon: number };
  connected: boolean;
};

export type HighScores = Record<GameId, number>;
