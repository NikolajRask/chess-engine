import { useCallback, useMemo, useState } from 'react'
import { Chess } from 'chess.js'
import { Chessboard } from 'react-chessboard'
import './App.css'

const API_URL = import.meta.env.VITE_API_URL ?? 'http://localhost:8080'

type BestMoveResponse = {
  bestmove: string
  scoreCp: number
  depth: number
}

function uciToMove(uci: string) {
  const from = uci.slice(0, 2)
  const to = uci.slice(2, 4)
  const promotion = uci.length > 4 ? uci[4] : undefined
  return { from, to, promotion }
}

function App() {
  const [game, setGame] = useState(() => new Chess())
  const [fen, setFen] = useState(game.fen())
  const [thinking, setThinking] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [lastEngine, setLastEngine] = useState<BestMoveResponse | null>(null)
  const [timeMs, setTimeMs] = useState(2000)
  const [orientation, setOrientation] = useState<'white' | 'black'>('white')

  const playerColor = 'w' as const
  const status = useMemo(() => {
    if (game.isCheckmate()) return 'Checkmate'
    if (game.isStalemate()) return 'Stalemate'
    if (game.isDraw()) return 'Draw'
    if (game.isCheck()) return 'Check'
    return thinking ? 'Computer thinking…' : 'Your move'
  }, [game, thinking])

  const requestComputerMove = useCallback(
    async (current: Chess) => {
      setThinking(true)
      setError(null)
      try {
        const res = await fetch(`${API_URL}/api/bestmove`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            fen: current.fen(),
            timeMs,
            maxDepth: 12,
          }),
        })
        if (!res.ok) {
          const body = await res.text()
          throw new Error(body || `HTTP ${res.status}`)
        }
        const data = (await res.json()) as BestMoveResponse
        const next = new Chess(current.fen())
        const move = uciToMove(data.bestmove)
        const result = next.move(move)
        if (!result) {
          throw new Error(`Engine returned illegal move: ${data.bestmove}`)
        }
        setGame(next)
        setFen(next.fen())
        setLastEngine(data)
      } catch (e) {
        setError(e instanceof Error ? e.message : 'Failed to reach chess server')
      } finally {
        setThinking(false)
      }
    },
    [timeMs],
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

      const next = new Chess(game.fen())
      const move = next.move({
        from: sourceSquare,
        to: targetSquare,
        promotion: 'q',
      })
      if (!move) return false

      setGame(next)
      setFen(next.fen())
      setLastEngine(null)
      setError(null)

      if (!next.isGameOver()) {
        void requestComputerMove(next)
      }
      return true
    },
    [game, thinking, requestComputerMove],
  )

  const newGame = () => {
    const fresh = new Chess()
    setGame(fresh)
    setFen(fresh.fen())
    setError(null)
    setLastEngine(null)
    setThinking(false)
  }

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
      <header>
        <h1>Chess AI</h1>
        <p>Play as White against the C++ engine.</p>
      </header>

      <main className="game">
        <div className="board-wrap">
          <Chessboard options={chessboardOptions} />
        </div>

        <aside className="panel">
          <p className="status">{status}</p>
          {error && <p className="error">{error}</p>}
          {lastEngine && (
            <p className="engine-info">
              Engine: {lastEngine.bestmove} (depth {lastEngine.depth}, score{' '}
              {(lastEngine.scoreCp / 100).toFixed(2)})
            </p>
          )}

          <label>
            Time per move (ms)
            <input
              type="range"
              min={500}
              max={5000}
              step={250}
              value={timeMs}
              disabled={thinking}
              onChange={(e) => setTimeMs(Number(e.target.value))}
            />
            <span>{timeMs} ms</span>
          </label>

          <div className="actions">
            <button type="button" onClick={newGame} disabled={thinking}>
              New game
            </button>
            <button
              type="button"
              onClick={() => setOrientation((o) => (o === 'white' ? 'black' : 'white'))}
              disabled={thinking}
            >
              Flip board
            </button>
          </div>
        </aside>
      </main>
    </div>
  )
}

export default App
