import type { HitEvent } from "../plugin/types";

export function HitEventBox({ event }: { event: HitEvent }) {
  return <div class="px-2">{`${event.attackerName} ${event.targetName} ${event.damage}`}</div>;
}
