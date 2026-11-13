import { createRenderEffect, type Accessor } from "solid-js";

export function createCssPropDirective(propName: string) {
  return (el: HTMLElement, accessor: () => Accessor<string | number>) => {
    const signal = accessor();
    createRenderEffect(() => {
      el.style.setProperty(propName, String(signal()));
    });
  };
}


