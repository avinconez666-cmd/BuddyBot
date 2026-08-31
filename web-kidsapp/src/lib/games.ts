import type { GameId } from "./types";

export const GAMES: {
  id: GameId;
  title: string;
  skill: string;
  age: string;
}[] = [
  { id: "jump", title: "Buddy Run", skill: "Jump the boxes", age: "3+" },
  { id: "maze", title: "Maze Munch", skill: "Eat the dots", age: "3+" },
  { id: "starship", title: "Starship", skill: "Dodge the rocks", age: "3+" },
  { id: "memory", title: "Matrix Memory", skill: "Find the pairs", age: "3+" },
  { id: "color", title: "Color Match", skill: "Tap the color", age: "2+" },
  { id: "math", title: "Math Blast", skill: "Pick the answer", age: "4+" },
];
