import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { Chess, type Move as ChessMove } from 'chess.js'
import { Chessboard } from 'react-chessboard'
import './App.css'

const API_URL = import.meta.env.VITE_API_URL ?? 'http://localhost:8080'
const START_FEN = new Chess().fen()

type BestMoveResponse = {
  bestmove: string
  scoreCp: number
  depth: number
  fromBook?: boolean
}

type Snapshot = {
  fen: string
  engine: BestMoveResponse | null
}

function uciToMove(uci: string) {
  const from = uci.slice(0, 2)
  const to = uci.slice(2, 4)
  const promotion = uci.length > 4 ? uci[4] : undefined
  return { from, to, promotion }
}

function skillLabel(skill: number) {
  if (skill <= 5) return 'Beginner'
  if (skill <= 12) return 'Intermediate'
  if (skill <= 19) return 'Advanced'
  return 'Max'
}

function pairedMoveRows(history: ChessMove[]) {
  const rows: { n: number; white?: string; black?: string }[] = []
  for (let i = 0; i < history.length; i += 2) {
    rows.push({
      n: Math.floor(i / 2) + 1,
      white: history[i]?.san,
      black: history[i + 1]?.san,
    })
  }
  return rows
}

function App() {
  const [history, setHistory] = useState<Snapshot[]>([{ fen: START_FEN, engine: null }])
  const [cursor, setCursor] = useState(0)
  const [thinking, setThinking] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [timeMs, setTimeMs] = useState(2000)
  const [threads, setThreads] = useState(1)
  const [skillLevel, setSkillLevel] = useState(20)
  const [useBook, setUseBook] = useState(true)
  const [orientation, setOrientation] = useState<'white' | 'black'>('white')

  const abortRef = useRef<AbortController | null>(null)
  const requestIdRef = useRef(0)
  const [boardEntered, setBoardEntered] = useState(true)

  useEffect(() => {
    const id = window.setTimeout(() => setBoardEntered(false), 900)
    return () => window.clearTimeout(id)
  }, [])

  const snapshot = history[cursor]
  const fen = snapshot.fen
  const lastEngine = snapshot.engine
  const game = useMemo(() => new Chess(fen), [fen])
  const playerColor = 'w' as const

  const canUndo = cursor > 0 && !thinking
  const canRedo = cursor < history.length - 1 && !thinking

  const status = useMemo(() => {
    if (game.isCheckmate()) return 'Checkmate'
    if (game.isStalemate()) return 'Stalemate'
    if (game.isDraw()) return 'Draw'
    if (game.isCheck()) return 'Check'
    return thinking ? 'Engine thinking…' : 'Your move'
  }, [game, thinking])

  const moveRows = useMemo(() => pairedMoveRows(game.history({ verbose: true })), [game])

  const pushSnapshot = useCallback((next: Snapshot, fromCursor: number) => {
    setHistory((prev) => [...prev.slice(0, fromCursor + 1), next])
    setCursor(fromCursor + 1)
  }, [])

  const applyCursor = useCallback((nextCursor: number) => {
    setCursor(nextCursor)
    setError(null)
  }, [])

  const undo = useCallback(() => {
    if (thinking) {
      abortRef.current?.abort()
      abortRef.current = null
      setThinking(false)
      // Player already moved; step back to before that move.
      setCursor((c) => Math.max(0, c - 1))
      setError(null)
      return
    }
    if (cursor <= 0) return
    let next = cursor - 1
    // Undo a full turn (player + engine) when possible so it's White to move.
    const at = new Chess(history[next].fen)
    if (at.turn() === 'b' && next > 0) next -= 1
    applyCursor(next)
  }, [thinking, cursor, history, applyCursor])

  const redo = useCallback(() => {
    if (thinking || cursor >= history.length - 1) return
    let next = cursor + 1
    applyCursor(next)
    // If redo lands on player-only position and engine reply exists, advance once more.
    if (next < history.length - 1) {
      const mid = new Chess(history[next].fen)
      if (mid.turn() === 'b') {
        applyCursor(next + 1)
      }
    }
  }, [thinking, cursor, history, applyCursor])

  const requestComputerMove = useCallback(
    async (currentFen: string, fromCursor: number) => {
      abortRef.current?.abort()
      const controller = new AbortController()
      abortRef.current = controller
      const requestId = ++requestIdRef.current

      setThinking(true)
      setError(null)
      try {
        const res = await fetch(`${API_URL}/api/bestmove`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          signal: controller.signal,
          body: JSON.stringify({
            fen: currentFen,
            timeMs,
            threads,
            skillLevel,
            useBook,
          }),
        })
        if (!res.ok) {
          const body = await res.text()
          throw new Error(body || `HTTP ${res.status}`)
        }
        const data = (await res.json()) as BestMoveResponse
        if (requestId !== requestIdRef.current) return

        const next = new Chess(currentFen)
        const move = uciToMove(data.bestmove)
        const result = next.move(move)
        if (!result) {
          throw new Error(`Engine returned illegal move: ${data.bestmove}`)
        }
        pushSnapshot({ fen: next.fen(), engine: data }, fromCursor)
      } catch (e) {
        if (e instanceof DOMException && e.name === 'AbortError') return
        if (requestId !== requestIdRef.current) return
        setError(e instanceof Error ? e.message : 'Failed to reach chess server')
      } finally {
        if (requestId === requestIdRef.current) {
          setThinking(false)
          abortRef.current = null
        }
      }
    },
    [timeMs, threads, skillLevel, useBook, pushSnapshot],
  )

  const onPieceDrop = useCallback(
    ({
      sourceSquare,
      targetSquare,
    }: {
      piece: unknown
      sourceSquare: string
      targetSquare: string | null
    }) => {
      if (!targetSquare || thinking) return false
      if (game.turn() !== playerColor) return false

      const next = new Chess(fen)
      const move = next.move({
        from: sourceSquare,
        to: targetSquare,
        promotion: 'q',
      })
      if (!move) return false

      const playerCursor = cursor
      pushSnapshot({ fen: next.fen(), engine: null }, playerCursor)
      setError(null)

      if (!next.isGameOver()) {
        void requestComputerMove(next.fen(), playerCursor + 1)
      }
      return true
    },
    [thinking, game, fen, cursor, pushSnapshot, requestComputerMove],
  )

  const newGame = () => {
    abortRef.current?.abort()
    abortRef.current = null
    requestIdRef.current += 1
    setThinking(false)
    setHistory([{ fen: START_FEN, engine: null }])
    setCursor(0)
    setError(null)
  }

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const meta = e.metaKey || e.ctrlKey
      if (!meta) return
      if (e.key === 'z' || e.key === 'Z') {
        e.preventDefault()
        if (e.shiftKey) redo()
        else undo()
      } else if (e.key === 'y' || e.key === 'Y') {
        e.preventDefault()
        redo()
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [undo, redo])

  const chessboardOptions = useMemo(
    () => ({
      position: fen,
      boardOrientation: orientation,
      allowDragging: !thinking && game.turn() === playerColor && !game.isGameOver(),
      onPieceDrop,
    }),
    [fen, orientation, thinking, game, onPieceDrop],
  )

  return (
    <div className="layout">
      <header className="brand">
        <div>
          <h1>Chess AI</h1>
          <p>Play White against the native C++ engine.</p>
        </div>
      </header>

      <main className="game">
        <div className="board-column">
          <div
            className={`board-wrap${boardEntered ? ' board-enter' : ''}${thinking ? ' thinking' : ''}`}
          >
            <Chessboard options={chessboardOptions} />
          </div>

          <div className="toolbar">
            <button type="button" className="btn btn-ghost" onClick={undo} disabled={!canUndo && !thinking}>
              Undo
            </button>
            <button type="button" className="btn btn-ghost" onClick={redo} disabled={!canRedo}>
              Redo
            </button>
            <button type="button" className="btn btn-ghost" onClick={() => setOrientation((o) => (o === 'white' ? 'black' : 'white'))}>
              Flip
            </button>
            <button type="button" className="btn btn-primary" onClick={newGame} disabled={thinking}>
              New game
            </button>
          </div>
          <p className="hint">⌘/Ctrl+Z undo · ⌘/Ctrl+Shift+Z redo</p>
        </div>

        <aside className="panel">
          <div className="status-block">
            <p className="status">{status}</p>
            <p className="status-meta">
              {skillLabel(skillLevel)} · {timeMs} ms · {threads} thread{threads === 1 ? '' : 's'}
              {useBook ? ' · book on' : ''}
            </p>
          </div>

          {error && <p className="error">{error}</p>}

          {lastEngine && (
            <p className="engine-info">
              {lastEngine.fromBook
                ? `Book: ${lastEngine.bestmove}`
                : `${lastEngine.bestmove} · depth ${lastEngine.depth} · ${(lastEngine.scoreCp / 100).toFixed(2)}`}
            </p>
          )}

          <div className="moves" aria-label="Move list">
            {moveRows.length > 0 && (
              <ol>
                {moveRows.map((row) => (
                  <li key={row.n}>
                    <span className="san">{row.white ?? ''}</span>
                    {row.black ? (
                      <>
                        {' '}
                        <span className="san">{row.black}</span>
                      </>
                    ) : null}
                  </li>
                ))}
              </ol>
            )}
          </div>

          <section className="settings">
            <h2>Settings</h2>

            <label className="field">
              Difficulty · <span className="value">{skillLabel(skillLevel)} ({skillLevel})</span>
              <input
                type="range"
                min={0}
                max={20}
                step={1}
                value={skillLevel}
                disabled={thinking}
                onChange={(e) => setSkillLevel(Number(e.target.value))}
              />
            </label>

            <label className="field">
              Time per move · <span className="value">{timeMs} ms</span>
              <input
                type="range"
                min={500}
                max={5000}
                step={250}
                value={timeMs}
                disabled={thinking}
                onChange={(e) => setTimeMs(Number(e.target.value))}
              />
            </label>

            <label className="field">
              Threads · <span className="value">{threads}</span>
              <input
                type="range"
                min={1}
                max={8}
                step={1}
                value={threads}
                disabled={thinking}
                onChange={(e) => setThreads(Number(e.target.value))}
              />
            </label>

            <label className="check">
              <input
                type="checkbox"
                checked={useBook}
                disabled={thinking}
                onChange={(e) => setUseBook(e.target.checked)}
              />
              Opening book
            </label>
          </section>
        </aside>
      </main>
    </div>
  )
}

export default App
