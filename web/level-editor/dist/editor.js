/* Offline, dependency-free editor. All geometry uses the firmware's five lanes. */
(() => {
  "use strict";
  const C = window.RSCodec,
    $ = (id) => document.getElementById(id);
  const tools = [
    {
      key: "select",
      name: "选择 / 移动",
      symbol: "↖",
      color: "#c5d6ed",
      group: "编辑",
      note: "点击查看格子。拖动已有格子，可将地板和物件一起移动到目标格，覆盖目标内容。",
    },
    {
      key: "erase",
      name: "清除物件",
      symbol: "⌫",
      color: "#c5d6ed",
      group: "编辑",
      note: "清除障碍、跳板、机关和钻石，保留当前地板。",
    },
    {
      key: "floor",
      name: "普通地板",
      symbol: "▱",
      color: "#9bbddf",
      group: "基础地块",
      note: "铺设地板并清除静态物件和机关。钻石保留。",
    },
    {
      key: "hole",
      name: "空缺",
      symbol: "⊘",
      color: "#8194ac",
      group: "基础地块",
      note: "移除本格地板和全部物件。",
    },
    {
      key: "tile1",
      name: "跳板",
      symbol: "⇈",
      color: "#75e4aa",
      group: "基础地块",
      note: "自动铺设地板，放置普通跳板。",
    },
    {
      key: "tile6",
      name: "短跳板",
      symbol: "↟",
      color: "#c5ef78",
      group: "基础地块",
      note: "自动铺设地板，放置短跳板。",
    },
    {
      key: "tile3",
      name: "玻璃",
      symbol: "▧",
      color: "#8de9ff",
      group: "障碍与收集",
      note: "玻璃需要地板支撑，不能叠加其他静态物件。",
    },
    {
      key: "tile4",
      name: "实体障碍",
      symbol: "⬢",
      color: "#f0b782",
      group: "障碍与收集",
      note: "实体方块。会清除本格钻石，避免钻石位于障碍内部。",
    },
    {
      key: "tile2",
      name: "低墙",
      symbol: "▬",
      color: "#f3a7ac",
      group: "障碍与收集",
      note: "需要跳过的低墙。受现有绘图缓存限制，两排低墙应间隔至少 16 行。",
    },
    {
      key: "tile5",
      name: "空中障碍",
      symbol: "▰",
      color: "#b6a4ff",
      group: "障碍与收集",
      note: "空中障碍可以悬在空缺上方，需考虑跳跃路线。",
    },
    {
      key: "tile7",
      name: "伤害地块",
      symbol: "✹",
      color: "#ff898e",
      group: "障碍与收集",
      note: "危险地块需要地板，会清除本格钻石。",
    },
    {
      key: "gem",
      name: "钻石",
      symbol: "◆",
      color: "#7df3e5",
      group: "障碍与收集",
      note: "每关最多 32 颗。可以放在空缺或玻璃上，不能放进低墙、实体障碍和伤害地块。",
    },
    {
      key: "mech1",
      name: "激光",
      symbol: "⌁",
      color: "#ff92c2",
      group: "动态机关",
      note: "周期激光。放下后，用选择工具点击该格调整周期、相位和范围。",
    },
    {
      key: "mech2",
      name: "横移障碍",
      symbol: "↔",
      color: "#ffc278",
      group: "动态机关",
      note: "横向往复移动的障碍物。每关动态机关合计最多 32 个。",
    },
    {
      key: "mech3",
      name: "竖摆锤",
      symbol: "♟",
      color: "#a6afff",
      group: "动态机关",
      note: "竖向摆锤。参数为锤头高度，256～680。",
    },
    {
      key: "mech4",
      name: "滚轮",
      symbol: "◉",
      color: "#a1d8ff",
      group: "动态机关",
      note: "滚轮障碍。密集放置时注意导出检查中的绘图预算提示。",
    },
    {
      key: "mech5",
      name: "飞锤",
      symbol: "⚒",
      color: "#e4b4ff",
      group: "动态机关",
      note: "飞锤机关。自动建立支撑地板，并清除本格静态物件。",
    },
  ];
  const toolMap = new Map(tools.map((t) => [t.key, t]));
  let levels = C.fromJSON(window.RS_SEED),
    active = 0,
    selected = null,
    tool = "select",
    rowHeight = 68;
  let history = [],
    future = [],
    stroke = null,
    renderPending = false,
    saveTimer,
    toastTimer,
    db = null;
  let moveDrag = null;
  const dragGhost = document.createElement("div");
  dragGhost.className = "drag-ghost";
  dragGhost.hidden = true;
  document.body.append(dragGhost);
  function showDrag(text, event) {
    dragGhost.textContent = text;
    dragGhost.style.left = `${event.clientX + 14}px`;
    dragGhost.style.top = `${event.clientY + 14}px`;
    dragGhost.hidden = false;
  }
  const level = () => levels[active];
  const clone = (l) => ({
    ...l,
    data: new Uint8Array(l.data),
    gems: l.gems.map((g) => [...g]),
    mechs: l.mechs.map((m) => [...m]),
  });
  function toast(message) {
    $("toast").textContent = message;
    $("toast").hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => ($("toast").hidden = true), 3800);
  }
  function dialog(title, text, actions = []) {
    $("modal-title").textContent = title;
    $("modal-body").textContent = text;
    $("modal-actions").replaceChildren();
    for (const action of actions) {
      const b = document.createElement("button");
      b.textContent = action.label;
      if (action.primary) b.className = "primary";
      b.onclick = () => {
        $("modal").close();
        action.run?.();
      };
      $("modal-actions").append(b);
    }
    $("modal").showModal();
  }
  $("modal-close").onclick = () => $("modal").close();
  function fail(error) {
    dialog("请检查一下", error.message || String(error), [{ label: "知道了" }]);
  }
  function guarded(fn) {
    return (...args) => {
      try {
        return fn(...args);
      } catch (error) {
        fail(error);
      }
    };
  }
  function nextID() {
    const used = new Set(levels.map((l) => l.id));
    let id = 1;
    while (used.has(id)) id++;
    return id;
  }
  function cellState(l, row, lane) {
    const bit = 1 << lane;
    return {
      masks: Array.from(l.data.subarray(row * 8, row * 8 + 8), (m) =>
        Boolean(m & bit),
      ),
      gem: l.gems.some((g) => g[0] === row && g[1] === lane + 1),
      mech:
        l.mechs.find((m) => m[0] === row && m[1] === lane + 1)?.slice() ?? null,
    };
  }
  function setCell(l, row, lane, state) {
    const bit = 1 << lane;
    for (let k = 0; k < 8; k++)
      l.data[row * 8 + k] =
        (l.data[row * 8 + k] & ~bit) | (state.masks[k] ? bit : 0);
    l.gems = l.gems.filter((g) => g[0] !== row || g[1] !== lane + 1);
    l.mechs = l.mechs.filter((m) => m[0] !== row || m[1] !== lane + 1);
    if (state.gem) l.gems.push([row, lane + 1]);
    if (state.mech) l.mechs.push([row, lane + 1, ...state.mech.slice(2)]);
  }
  function remember(row, lane) {
    const key = row * 5 + lane;
    if (!stroke.changes.has(key))
      stroke.changes.set(key, {
        row,
        lane,
        before: cellState(level(), row, lane),
      });
  }
  function startStroke() {
    stroke = { index: active, changes: new Map(), last: null };
  }
  function commitStroke() {
    if (!stroke) return;
    const changes = [...stroke.changes.values()]
      .map((c) => ({
        ...c,
        after: cellState(levels[stroke.index], c.row, c.lane),
      }))
      .filter((c) => JSON.stringify(c.before) !== JSON.stringify(c.after));
    if (changes.length) record({ type: "cells", index: stroke.index, changes });
    stroke = null;
    refreshSide();
  }
  function record(action) {
    history.push(action);
    if (history.length > 40) history.shift();
    future = [];
    changed();
  }
  function replaceLevel(next) {
    const before = clone(level());
    levels[active] = next;
    record({ type: "replace", index: active, before, after: clone(next) });
    refresh();
  }
  function applyHistory(action, forward) {
    if (action.type === "cells") {
      active = action.index;
      for (const c of action.changes)
        setCell(level(), c.row, c.lane, forward ? c.after : c.before);
    } else if (action.type === "replace") {
      active = action.index;
      levels[active] = clone(forward ? action.after : action.before);
    } else {
      const insert = (action.type === "add") === forward;
      if (insert) levels.splice(action.index, 0, clone(action.value));
      else levels.splice(action.index, 1);
      active = Math.min(action.index, levels.length - 1);
    }
    selected = null;
    changed();
    refresh();
  }
  $("undo").onclick = () => {
    commitStroke();
    const a = history.pop();
    if (a) {
      future.push(a);
      applyHistory(a, false);
    }
  };
  $("redo").onclick = () => {
    commitStroke();
    const a = future.pop();
    if (a) {
      history.push(a);
      applyHistory(a, true);
    }
  };
  function changed() {
    scheduleRouteCheck();
    $("undo").disabled = !history.length;
    $("redo").disabled = !future.length;
    $("save-state").textContent = db ? "正在保存草稿…" : "浏览器内编辑";
    clearTimeout(saveTimer);
    saveTimer = setTimeout(save, 550);
  }
  function save() {
    if (!db) {
      $("save-state").textContent = "请保存 JSON 留存草稿";
      return;
    }
    try {
      const transaction = db.transaction("drafts", "readwrite");
      transaction.objectStore("drafts").put({ levels, active }, "current");
      transaction.oncomplete = () =>
        ($("save-state").textContent = "✓ 草稿已保存在此浏览器");
      transaction.onerror = () =>
        ($("save-state").textContent = "保存失败，请保存 JSON");
    } catch {
      $("save-state").textContent = "保存失败，请保存 JSON";
    }
  }
  function validate() {
    const box = $("validation");
    box.replaceChildren();
    try {
      const warnings = C.validate(levels);
      const ok = document.createElement("p");
      ok.className = "ok";
      ok.textContent = "✓ 格式与板端容量检查通过";
      box.append(ok);
      for (const warning of warnings) {
        const p = document.createElement("p");
        p.className = "warning";
        p.textContent = warning;
        box.append(p);
      }
      return true;
    } catch (error) {
      const p = document.createElement("p");
      p.className = "error";
      p.textContent = error.message;
      box.append(p);
      return false;
    }
  }
  $("validate").onclick = validate;
  let routeTimer, routeResult;
  function scheduleRouteCheck(delay = 350) {
    clearTimeout(routeTimer);
    routeResult = null;
    const status = $("route-status");
    status.textContent = "正在模拟从起点到终点的路线…";
    status.className = "hint";
    $("route-jump").hidden = true;
    const search = window.RSReachability.analyze(level());
    function run() {
      const deadline = performance.now() + 8;
      let update;
      do {
        update = search.next();
      } while (!update.done && performance.now() < deadline);
      if (!update.done) {
        routeTimer = setTimeout(run, 0);
        return;
      }
      routeResult = update.value;
      const { status: result, row, limited, uncertainCollision } = routeResult;
      status.className = result === "pass" ? "ok" : result === "blocked" ? "error" : "warning";
      if (result === "pass") {
        status.textContent = "理论可通关。按 33/33/34 毫秒的帧节奏，找到至少一种按键走法。";
        return;
      }
      status.textContent = result === "blocked"
        ? `确定堵死。即使允许每步自由调整横向位置，路线也在第 ${row} 行附近全部中断。`
        : uncertainCollision
          ? `未能确认。第 ${row} 行附近的飞锤超出了可可靠模拟的碰撞范围。`
          : limited
          ? `未能确认。搜索达到计算上限，参考位置为第 ${row} 行。`
          : `按键模拟在第 ${row} 行附近未找到走法。更灵活的操控仍有可能通过。`;
      $("route-jump").textContent = `定位到第 ${row} 行`;
      $("route-jump").hidden = false;
    }
    routeTimer = setTimeout(run, delay);
  }
  $("check-route").onclick = () => scheduleRouteCheck(0);
  $("route-jump").onclick = () => {
    if (!routeResult) return;
    $("jump-row").value = routeResult.row;
    $("jump").click();
  };
  function selectTool(key) {
    tool = key;
    for (const b of $("palette").querySelectorAll("button")) {
      b.classList.toggle("active", b.dataset.tool === key);
      b.setAttribute("aria-pressed", String(b.dataset.tool === key));
    }
    $("tool-note").textContent = toolMap.get(key).note;
    scheduleGrid();
  }
  for (const group of [...new Set(tools.map((t) => t.group))]) {
    const label = document.createElement("div");
    label.className = "palette-group";
    label.textContent = group;
    $("palette").append(label);
    const grid = document.createElement("div");
    grid.className = "palette-grid";
    for (const t of tools.filter((t) => t.group === group)) {
      const button = document.createElement("button");
      button.className = "tool";
      button.dataset.tool = t.key;
      button.title = t.note;
      button.style.setProperty("--tool-color", t.color);
      const symbol = document.createElement("span");
      symbol.className = "symbol";
      symbol.textContent = t.symbol;
      button.append(symbol, document.createTextNode(t.name));
      button.onclick = () => selectTool(t.key);
      let dragging = false;
      button.onpointerdown = (e) => {
        if (e.button !== 0 || t.key === "select") return;
        e.preventDefault();
        commitStroke();
        selectTool(t.key);
        dragging = true;
        button.setPointerCapture(e.pointerId);
      };
      button.onpointermove = (e) => {
        if (dragging) showDrag(`${t.symbol} ${t.name}`, e);
      };
      button.onpointerup = (e) => {
        if (!dragging) return;
        dragging = false;
        dragGhost.hidden = true;
        const cell = targetCell(e);
        if (cell) {
          startStroke();
          paint(cell.row, cell.lane, t.key);
          commitStroke();
        }
      };
      button.onpointercancel = button.onlostpointercapture = () => {
        dragging = false;
        dragGhost.hidden = true;
      };
      grid.append(button);
    }
    $("palette").append(grid);
  }
  function renderCatalog() {
    $("catalog").replaceChildren();
    $("catalog-count").textContent = `${levels.length} / 24`;
    levels.forEach((l, index) => {
      const button = document.createElement("button");
      button.className = `catalog-item${index === active ? " active" : ""}`;
      for (const [cls, value] of [
        ["id", String(l.id).padStart(2, "0")],
        ["name", l.name],
        ["rows", `${l.rows} 行`],
      ]) {
        const s = document.createElement("span");
        s.className = cls;
        s.textContent = value;
        button.append(s);
      }
      button.onclick = () => {
        commitStroke();
        active = index;
        selected = null;
        $("grid-scroll").scrollTop = 0;
        refresh();
        changed();
      };
      $("catalog").append(button);
    });
  }
  function refreshSide() {
    const l = level();
    $("stat-rows").textContent = l.rows.toLocaleString();
    $("stat-gems").textContent = `${l.gems.length}/32`;
    $("stat-mechs").textContent = `${l.mechs.length}/32`;
    $("clear-cell").disabled = !selected;
    $("mechanism-settings").hidden = true;
    if (selected && selected.row < l.rows) {
      const { row, lane } = selected,
        state = cellState(l, row, lane),
        names = [];
      $("selected-position").textContent = `第 ${row} 行 · ${lane + 1} 列`;
      names.push(state.masks[0] ? "普通地板" : "空缺");
      for (let k = 1; k < 8; k++)
        if (state.masks[k]) names.push(toolMap.get(`tile${k}`).name);
      if (state.gem) names.push("钻石");
      if (state.mech) {
        const m = state.mech;
        names.push(toolMap.get(`mech${m[2]}`).name);
        $("mechanism-settings").hidden = false;
        $("mech-period").value = m[3];
        $("mech-phase").value = m[4];
        $("mech-param").value = m[5];
        $("mech-phase").max = m[3] - 1;
        const min = m[2] === 3 ? 256 : 1,
          max = { 1: 640, 3: 680, 5: 900 }[m[2]] ?? 512;
        $("mech-param").min = min;
        $("mech-param").max = max;
        $("param-label").textContent = [
          "",
          "激光范围",
          "横移幅度",
          "锤头高度",
          "滚轮幅度",
          "飞锤范围",
        ][m[2]];
        $("param-help").textContent =
          `参数范围 ${min}～${max}，使用板端原始单位。周期按前进距离驱动，1024 对应 4 行。`;
      }
      $("cell-description").textContent = names.join(" · ");
    } else {
      selected = null;
      $("selected-position").textContent = "未选择";
      $("cell-description").textContent =
        "使用“选择”工具点击格子，查看或修改机关参数。";
    }
    $("undo").disabled = !history.length;
    $("redo").disabled = !future.length;
    validate();
  }
  function refresh() {
    scheduleRouteCheck();
    const l = level();
    renderCatalog();
    $("active-name").textContent = l.name;
    $("level-name").value = l.name;
    $("level-id").value = l.id;
    $("length").value = l.rows;
    $("jump-row").max = l.rows - 1;
    $("grid-space").style.height = `${l.rows * rowHeight}px`;
    refreshSide();
    scheduleGrid();
  }
  function scheduleGrid() {
    if (!renderPending) {
      renderPending = true;
      requestAnimationFrame(() => {
        renderPending = false;
        renderGrid();
      });
    }
  }
  function renderGrid() {
    const scroller = $("grid-scroll"),
      l = level(),
      first = Math.max(0, Math.floor(scroller.scrollTop / rowHeight) - 3),
      end = Math.min(
        l.rows,
        Math.ceil((scroller.scrollTop + scroller.clientHeight) / rowHeight) + 3,
      );
    const fragment = document.createDocumentFragment();
    for (let r = first; r < end; r++) {
      const row = document.createElement("div");
      row.className = "grid-row";
      row.style.top = `${r * rowHeight}px`;
      row.style.height = `${rowHeight}px`;
      const number = document.createElement("span");
      number.className = "row-number";
      number.textContent = String(r).padStart(3, "0");
      row.append(number);
      for (let lane = 0; lane < 5; lane++) {
        const state = cellState(l, r, lane),
          cell = document.createElement("button");
        cell.type = "button";
        cell.className = `cell${state.masks[0] ? " ground" : ""}${selected?.row === r && selected.lane === lane ? " selected" : ""}`;
        cell.dataset.row = r;
        cell.dataset.lane = lane;
        const objects = [];
        for (let k = 1; k < 8; k++)
          if (state.masks[k]) objects.push(toolMap.get(`tile${k}`));
        if (state.mech) objects.push(toolMap.get(`mech${state.mech[2]}`));
        const description = objects.map((t) => t.name);
        if (state.gem) description.push("钻石");
        cell.setAttribute(
          "aria-label",
          `第 ${r} 行，第 ${lane + 1} 列，${state.masks[0] ? "地板" : "空缺"}${description.length ? "，" + description.join("、") : ""}`,
        );
        cell.title = cell.getAttribute("aria-label");
        if (objects.length) {
          const symbol = document.createElement("span");
          symbol.className = "object";
          symbol.textContent = objects[0].symbol;
          symbol.style.setProperty("--object-color", objects[0].color);
          cell.append(symbol);
        }
        if (objects.length > 1) {
          const multi = document.createElement("span");
          multi.className = "multi";
          multi.textContent = `+${objects.length - 1}`;
          cell.append(multi);
        }
        if (state.gem) {
          const gem = document.createElement("span");
          gem.className = "gem";
          gem.textContent = "◆";
          cell.append(gem);
        }
        row.append(cell);
      }
      fragment.append(row);
    }
    $("grid-rows").replaceChildren(fragment);
    $("view-range").textContent =
      `${Math.min(l.rows - 1, Math.floor(scroller.scrollTop / rowHeight))} / ${l.rows - 1}`;
  }
  function targetCell(event) {
    const node = document
      .elementFromPoint(event.clientX, event.clientY)
      ?.closest(".cell");
    return node && $("grid-scroll").contains(node)
      ? { row: Number(node.dataset.row), lane: Number(node.dataset.lane), node }
      : null;
  }
  function paint(row, lane, key) {
    const l = level(),
      state = cellState(l, row, lane);
    remember(row, lane);
    if (key === "gem") {
      if (state.masks[2] || state.masks[4] || state.masks[7]) {
        toast("钻石不能放在低墙、实体障碍或伤害地块内");
        return;
      }
      if (!state.gem && l.gems.length >= 32) {
        toast("每关最多 32 颗钻石");
        return;
      }
      state.gem = true;
    } else if (key === "hole") {
      state.masks.fill(false);
      state.gem = false;
      state.mech = null;
    } else {
      state.masks.fill(false, 1);
      state.mech = null;
      if (key === "erase") state.gem = false;
      else if (key === "floor") state.masks[0] = true;
      else if (key.startsWith("tile")) {
        const kind = Number(key.slice(4));
        state.masks[kind] = true;
        if (kind !== 5) state.masks[0] = true;
        if ([2, 4, 7].includes(kind)) state.gem = false;
      } else if (key.startsWith("mech")) {
        const exists = l.mechs.some((m) => m[0] === row && m[1] === lane + 1);
        if (!exists && l.mechs.length >= 32) {
          toast("每关最多 32 个动态机关");
          return;
        }
        const kind = Number(key.slice(4));
        state.masks[0] = true;
        state.gem = false;
        state.mech = [
          row,
          lane + 1,
          kind,
          2048,
          0,
          [0, 512, 192, 400, 160, 500][kind],
        ];
      }
    }
    setCell(l, row, lane, state);
    selected = { row, lane };
    scheduleGrid();
  }
  function paintLine(cell) {
    if (!stroke) return;
    const from = stroke.last ?? cell,
      steps = Math.max(
        Math.abs(cell.row - from.row),
        Math.abs(cell.lane - from.lane),
      );
    for (let i = 0; i <= steps; i++) {
      const t = steps ? i / steps : 0;
      paint(
        Math.round(from.row + (cell.row - from.row) * t),
        Math.round(from.lane + (cell.lane - from.lane) * t),
        tool,
      );
    }
    stroke.last = cell;
  }
  const scroller = $("grid-scroll");
  scroller.addEventListener("scroll", scheduleGrid, { passive: true });
  scroller.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    const cell = targetCell(e);
    if (!cell) return;
    selected = { row: cell.row, lane: cell.lane };
    if (tool === "select") {
      e.preventDefault();
      moveDrag = {
        row: cell.row,
        lane: cell.lane,
        x: e.clientX,
        y: e.clientY,
        moved: false,
      };
      scroller.setPointerCapture(e.pointerId);
      refreshSide();
      for (const b of scroller.querySelectorAll(".cell"))
        b.classList.toggle(
          "selected",
          Number(b.dataset.row) === cell.row &&
            Number(b.dataset.lane) === cell.lane,
        );
      return;
    }
    e.preventDefault();
    commitStroke();
    startStroke();
    scroller.setPointerCapture(e.pointerId);
    paintLine(cell);
  });
  scroller.addEventListener("pointermove", (e) => {
    if (moveDrag) {
      if (Math.hypot(e.clientX - moveDrag.x, e.clientY - moveDrag.y) > 6)
        moveDrag.moved = true;
      if (moveDrag.moved) showDrag("↗ 移动格子", e);
    } else if (stroke) {
      const cell = targetCell(e);
      if (cell) paintLine(cell);
    }
    if (!moveDrag && !stroke) return;
    const box = scroller.getBoundingClientRect();
    if (e.clientY < box.top + 30) scroller.scrollTop -= rowHeight / 2;
    else if (e.clientY > box.bottom - 30) scroller.scrollTop += rowHeight / 2;
  });
  scroller.addEventListener("pointerup", (e) => {
    if (moveDrag?.moved) {
      const cell = targetCell(e),
        from = moveDrag;
      if (cell && (cell.row !== from.row || cell.lane !== from.lane)) {
        startStroke();
        remember(from.row, from.lane);
        remember(cell.row, cell.lane);
        const content = cellState(level(), from.row, from.lane);
        setCell(level(), cell.row, cell.lane, content);
        setCell(level(), from.row, from.lane, {
          masks: [false, false, false, false, false, false, false, false],
          gem: false,
          mech: null,
        });
        selected = { row: cell.row, lane: cell.lane };
        scheduleGrid();
      }
    }
    moveDrag = null;
    dragGhost.hidden = true;
    commitStroke();
  });
  for (const event of ["pointercancel", "lostpointercapture"])
    scroller.addEventListener(event, () => {
      moveDrag = null;
      dragGhost.hidden = true;
      commitStroke();
    });
  scroller.addEventListener("click", (e) => {
    if (e.detail !== 0) return;
    const node = e.target.closest(".cell");
    if (!node) return;
    selected = {
      row: Number(node.dataset.row),
      lane: Number(node.dataset.lane),
    };
    if (tool !== "select") {
      startStroke();
      paint(selected.row, selected.lane, tool);
      commitStroke();
    } else refreshSide();
    scheduleGrid();
  });
  $("clear-cell").onclick = () => {
    if (!selected) return;
    startStroke();
    paint(selected.row, selected.lane, "erase");
    commitStroke();
  };
  $("apply-mech").onclick = guarded(() => {
    if (!selected) return;
    const { row, lane } = selected,
      state = cellState(level(), row, lane);
    if (!state.mech) return;
    state.mech[3] = Number($("mech-period").value);
    state.mech[4] = Number($("mech-phase").value);
    state.mech[5] = Number($("mech-param").value);
    const candidate = clone(level());
    setCell(candidate, row, lane, state);
    C.validate([candidate]);
    startStroke();
    remember(row, lane);
    setCell(level(), row, lane, state);
    commitStroke();
    toast("机关参数已更新");
  });
  $("new").onclick = guarded(() => {
    if (levels.length >= 24) throw new Error("最多 24 个关卡");
    const value = C.create(nextID(), `自定义关卡 ${nextID()}`);
    const index = levels.length;
    levels.push(value);
    active = index;
    selected = null;
    record({ type: "add", index, value: clone(value) });
    scroller.scrollTop = 0;
    refresh();
  });
  $("duplicate").onclick = guarded(() => {
    if (levels.length >= 24) throw new Error("最多 24 个关卡");
    const value = clone(level());
    value.id = nextID();
    value.name = `${value.name} 副本`.slice(0, 80);
    const index = levels.length;
    levels.push(value);
    active = index;
    selected = null;
    record({ type: "add", index, value: clone(value) });
    refresh();
  });
  $("delete-level").onclick = () => {
    if (levels.length === 1) {
      toast("请至少保留一个关卡");
      return;
    }
    dialog("删除关卡", `从目录移除“${level().name}”？可使用撤销恢复。`, [
      { label: "取消" },
      {
        label: "删除",
        run: () => {
          const index = active,
            value = clone(level());
          levels.splice(active, 1);
          active = Math.min(active, levels.length - 1);
          selected = null;
          record({ type: "delete", index, value });
          refresh();
        },
      },
    ]);
  };
  $("level-name").onchange = () => {
    const name = $("level-name").value.trim() || "未命名关卡";
    const next = clone(level());
    next.name = name;
    replaceLevel(next);
  };
  $("level-id").onchange = guarded(() => {
    const id = Number($("level-id").value);
    if (
      !Number.isInteger(id) ||
      id < 1 ||
      id > 65535 ||
      levels.some((l, i) => i !== active && l.id === id)
    ) {
      $("level-id").value = level().id;
      throw new Error("ID 需要在 1～65535 内且不与目录中其他关卡重复");
    }
    const next = clone(level());
    next.id = id;
    replaceLevel(next);
  });
  $("resize").onclick = guarded(() => {
    const rows = Number($("length").value);
    if (!Number.isInteger(rows) || rows < 16 || rows > 65535)
      throw new Error("长度应为 16～65535 的整数");
    if (rows === level().rows) return;
    const perform = () => {
      const old = level(),
        next = C.create(old.id, old.name, rows);
      next.data.set(old.data.subarray(0, Math.min(old.rows, rows) * 8));
      next.gems = old.gems.filter((g) => g[0] < rows).map((g) => [...g]);
      next.mechs = old.mechs.filter((m) => m[0] < rows).map((m) => [...m]);
      selected = null;
      replaceLevel(next);
      toast(`长度已调整为 ${rows} 行`);
    };
    if (rows < level().rows)
      dialog(
        "缩短关卡",
        `将删除第 ${rows} 行及之后的地板、钻石和机关。缩短后可撤销恢复。`,
        [
          { label: "取消", run: () => ($("length").value = level().rows) },
          { label: "确认缩短", primary: true, run: perform },
        ],
      );
    else perform();
  });
  $("jump").onclick = () => {
    const row = Math.max(
      0,
      Math.min(level().rows - 1, Number($("jump-row").value) || 0),
    );
    scroller.scrollTop = row * rowHeight;
  };
  $("jump-row").onkeydown = (e) => {
    if (e.key === "Enter") $("jump").click();
  };
  $("zoom").onchange = () => {
    const row = scroller.scrollTop / rowHeight;
    rowHeight = Number($("zoom").value);
    $("grid-space").style.height = `${level().rows * rowHeight}px`;
    scroller.scrollTop = row * rowHeight;
    scheduleGrid();
  };
  new ResizeObserver(scheduleGrid).observe(scroller);
  function download(filename, data, type) {
    const url = URL.createObjectURL(new Blob([data], { type })),
      a = document.createElement("a");
    a.href = url;
    a.download = filename;
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 30000);
  }
  $("draft").onclick = () => {
    commitStroke();
    download(
      "rolling-sky-levels.json",
      JSON.stringify(C.toJSON(levels), null, 2),
      "application/json",
    );
    toast("JSON 草稿已导出，可再次导入编辑");
  };
  $("export").onclick = guarded(() => {
    commitStroke();
    C.validate(levels);
    const bytes = C.encode(levels);
    const routeNotice = routeResult?.status === "blocked"
      ? `\n\n当前关卡的路线在第 ${routeResult.row} 行附近堵死，请检查后再写入 SD 卡。`
      : "";
    dialog(
      "导出 SD 关卡包",
      `包含目录内 ${levels.length} 个关卡，共 ${(bytes.length / 1024).toFixed(1)} KB。\n\n下载文件名为 RSLEVELS.BIN，复制到 FAT32 SD 卡根目录即可。已有同名文件请先备份，其他资源继续保留。\n\n开发板按目录顺序显示关卡编号。编辑名称保存在 JSON 草稿中。外观由原有 RSART.BIN 的稳定 ID 决定，新 ID 默认使用板端基础外观。${routeNotice}`,
      [
        { label: "返回编辑" },
        {
          label: "下载 RSLEVELS.BIN",
          primary: true,
          run: () => {
            download("RSLEVELS.BIN", bytes, "application/octet-stream");
            toast("关卡包已导出，请复制到 SD 卡根目录");
          },
        },
      ],
    );
  });
  $("import").onclick = () => $("file-input").click();
  $("file-input").onchange = async (e) => {
    const file = e.target.files[0];
    e.target.value = "";
    if (!file) return;
    try {
      if (file.size > 32 * 1024 * 1024)
        throw new Error("文件过大，请导入不超过 32 MB 的关卡 JSON 或 BIN");
      const imported = file.name.toLowerCase().endsWith(".json")
        ? C.fromJSON(JSON.parse(await file.text()))
        : C.decode(await file.arrayBuffer());
      dialog(
        "导入关卡目录",
        `已读取 ${imported.length} 关，将替换当前目录。需要保留当前作品时，请先取消并保存 JSON。`,
        [
          { label: "取消" },
          {
            label: "替换目录",
            primary: true,
            run: () => {
              levels = imported;
              active = 0;
              selected = null;
              history = [];
              future = [];
              scroller.scrollTop = 0;
              changed();
              refresh();
              toast("关卡目录已导入");
            },
          },
        ],
      );
    } catch (error) {
      fail(error);
    }
  };
  $("help").onclick = () =>
    dialog(
      "五分钟搭好一个关卡",
      "1. 点击“＋ 新关卡”，原有 5 关会保留在目录中。\n2. 将左侧地块或障碍拖到轨道格子里，也可以先点击工具，再按住鼠标连续涂画。行号从 0 开始，向下为游戏前进方向。\n3. 使用“选择 / 移动”点击机关格子，调整运动参数。选择模式下可拖动已有格子，移动会覆盖目标格并在原位置留下空缺。\n4. 在右侧输入长度并应用。每关支持 16～65,535 行、32 颗钻石和 32 个动态机关。低墙行间距至少 16 行。\n5. 导出 RSLEVELS.BIN，备份 SD 卡原文件后复制到根目录，安全弹出，开发板断电插回并上电。菜单 KEY4 重读，KEY6 / KEY14 选关，KEY8 开始。\n\n草稿自动保存在当前浏览器。保存 JSON 可备份、跨电脑继续编辑，Ctrl+S 快速导出草稿。Ctrl+Z 撤销，Ctrl+Shift+Z 重做。导入支持本项目 RSLEVEL4 二进制和 JSON 源文件。\n\n名字只保留在编辑草稿，板端按目录顺序显示编号。请在板上试玩验证路线、跳跃和机关时机。",
      [{ label: "开始搭建", primary: true }],
    );
  document.addEventListener("keydown", (e) => {
    const typing = e.target.matches("input,select,textarea");
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s") {
      e.preventDefault();
      $("draft").click();
    }
    if (typing || $("modal").open) return;
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z") {
      e.preventDefault();
      (e.shiftKey ? $("redo") : $("undo")).click();
    }
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "y") {
      e.preventDefault();
      $("redo").click();
    }
    if (e.key === "Delete" && selected) {
      e.preventDefault();
      $("clear-cell").click();
    }
  });
  selectTool("select");
  const newLevel = C.create(nextID(), "我的第一关", 128);
  levels.push(newLevel);
  active = levels.length - 1;
  refresh();
  try {
    const request = indexedDB.open("rolling-sky-level-studio", 1);
    request.onupgradeneeded = () => request.result.createObjectStore("drafts");
    request.onsuccess = () => {
      db = request.result;
      const read = db
        .transaction("drafts")
        .objectStore("drafts")
        .get("current");
      read.onsuccess = () => {
        if (read.result && history.length === 0) {
          try {
            C.validate(read.result.levels, { wallSpacing: false });
            levels = read.result.levels;
            active = Math.max(
              0,
              Math.min(levels.length - 1, read.result.active || 0),
            );
            selected = null;
            refresh();
            $("save-state").textContent = "✓ 已恢复此浏览器草稿";
          } catch {
            $("save-state").textContent = "旧草稿无效，请导入 JSON";
          }
        } else save();
      };
    };
    request.onerror = () =>
      ($("save-state").textContent = "请保存 JSON 留存草稿");
  } catch {
    $("save-state").textContent = "请保存 JSON 留存草稿";
  }
  window.addEventListener("pagehide", () => {
    commitStroke();
    clearTimeout(saveTimer);
    save();
  });
})();
