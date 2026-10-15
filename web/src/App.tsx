import { useCallback, useEffect, useMemo, useRef, useState, type CSSProperties, type ReactNode } from 'react'
import { Chess } from 'chess.js'
import { Chessboard } from 'react-chessboard'
import type { PieceDropHandlerArgs, PieceHandlerArgs, SquareHandlerArgs } from 'react-chessboard'
import './App.css'

// Prefer 127.0.0.1 over localhost — on macOS localhost can resolve to ::1 while the
// API binds IPv4 only, which surfaces as a cryptic "Failed to fetch".
const API_URL = import.meta.env.VITE_API_URL ?? 'http://127.0.0.1:8080'
const START_FEN = new Chess().fen()

const LAST_MOVE_STYLE: CSSProperties = { backgroundColor: 'rgba(247, 247, 105, 0.8)' }
const SELECTED_STYLE: CSSProperties = { backgroundColor: 'rgba(20, 85, 30, 0.45)' }
const CANDIDATE_STYLES: CSSProperties[] = [
  { backgroundColor: 'rgba(47, 107, 79, 0.45)' },
  { backgroundColor: 'rgba(47, 107, 79, 0.28)' },
  { backgroundColor: 'rgba(47, 107, 79, 0.16)' },
]

type PlayMode = 'human-white' | 'human-black' | 'analysis' | 'engine-engine'

type CandidateLine = {
  bestmove: string
  scoreCp: number
  scoreMate?: number | null
  pv?: string[]
}

type BestMoveResponse = {
  bestmove: string
  scoreCp: number
  scoreMate?: number | null
  depth: number
  nodes?: number
  timeMs?: number
  nps?: number
  pv?: string[]
  fromBook?: boolean
  gameOver?: boolean
  outcome?: string
  candidates?: CandidateLine[]
}

type FetchBestMoveOpts = {
  multiPv?: number
  skillLevel?: number
  useBook?: boolean
}

type BoardMove = { from: string; to: string }

type Snapshot = {
  fen: string
  engine: BestMoveResponse | null
  lastMove: BoardMove | null
  sans: string[]
  ucis: string[]
}

const PLAY_MODE_LABELS: Record<PlayMode, string> = {
  'human-white': 'Play White',
  'human-black': 'Play Black',
  analysis: 'Analysis',
  'engine-engine': 'Engine vs Engine',
}

function uciToMove(uci: string) {
  const from = uci.slice(0, 2)
  const to = uci.slice(2, 4)
  const promotion = uci.length > 4 ? uci[4] : undefined
  return { from, to, promotion }
}

function moveToUci(move: { from: string; to: string; promotion?: string | null }): string {
  return move.from + move.to + (move.promotion ?? '')
}

/** Rebuild a Chess instance from the full move list so draw rules (threefold, etc.) work. */
function gameFromUcis(ucis: string[]): Chess {
  const g = new Chess()
  for (const uci of ucis) {
    const result = g.move(uciToMove(uci))
    if (!result) break
  }
  return g
}

function drawStatusLabel(game: Chess): string | null {
  if (game.isCheckmate()) return 'Checkmate'
  if (game.isStalemate()) return 'Stalemate'
  if (game.isThreefoldRepetition()) return 'Draw by threefold repetition'
  if (game.isInsufficientMaterial()) return 'Draw by insufficient material'
  if (game.isDrawByFiftyMoves()) {
    const half = Number(game.fen().split(' ')[4] ?? 0)
    if (half >= 150) return 'Draw by seventy-five-move rule'
    return 'Draw by fifty-move rule'
  }
  if (game.isDraw()) return 'Draw'
  return null
}

function outcomeLabel(outcome: string | undefined): string | null {
  if (!outcome) return null
  switch (outcome) {
    case 'checkmate':
      return 'Checkmate'
    case 'stalemate':
      return 'Stalemate'
    case 'threefold':
      return 'Draw by threefold repetition'
    case 'fivefold':
      return 'Draw by fivefold repetition'
    case 'fifty_move':
      return 'Draw by fifty-move rule'
    case 'seventy_five_move':
      return 'Draw by seventy-five-move rule'
    case 'insufficient_material':
      return 'Draw by insufficient material'
    default:
      return outcome
  }
}

function skillLabel(skill: number) {
  if (skill <= 5) return 'Beginner'
  if (skill <= 12) return 'Intermediate'
  if (skill <= 19) return 'Advanced'
  return 'Max'
}

function pairedMoveRows(sans: string[]) {
  const rows: { n: number; white?: string; black?: string }[] = []
  for (let i = 0; i < sans.length; i += 2) {
    rows.push({
      n: Math.floor(i / 2) + 1,
      white: sans[i],
      black: sans[i + 1],
    })
  }
  return rows
}

function fenSideToMove(fen: string): 'w' | 'b' {
  return fen.split(' ')[1] === 'b' ? 'b' : 'w'
}

function flipScore<T extends { scoreCp: number; scoreMate?: number | null }>(line: T): T {
  return {
    ...line,
    scoreCp: -line.scoreCp,
    scoreMate:
      line.scoreMate == null || line.scoreMate === undefined ? line.scoreMate : -line.scoreMate,
  }
}

/** Convert engine STM-relative score to White's perspective. */
function toWhitePov(data: BestMoveResponse, searchFen: string): BestMoveResponse {
  const stm = fenSideToMove(searchFen)
  if (stm === 'w') return data
  return {
    ...flipScore(data),
    candidates: data.candidates?.map((c) => flipScore(c)),
  }
}

function formatEval(engine: {
  scoreCp: number
  scoreMate?: number | null
  fromBook?: boolean
}): string {
  if (engine.fromBook) return 'Book'
  if (engine.scoreMate != null && engine.scoreMate !== 0) {
    const m = engine.scoreMate
    return m > 0 ? `M${m}` : `-M${Math.abs(m)}`
  }
  const pawns = engine.scoreCp / 100
  const sign = pawns > 0 ? '+' : ''
  return `${sign}${pawns.toFixed(2)}`
}

function isUciLegal(fen: string, uci: string): boolean {
  try {
    const g = new Chess(fen)
    return Boolean(g.move(uciToMove(uci)))
  } catch {
    return false
  }
}

function evalBarPercent(engine: BestMoveResponse | null): number {
  if (!engine || engine.fromBook) return 50
  if (engine.scoreMate != null && engine.scoreMate !== 0) {
    return engine.scoreMate > 0 ? 100 : 0
  }
  const clamped = Math.max(-800, Math.min(800, engine.scoreCp))
  return 50 + (clamped / 800) * 50
}

function pvToSan(fen: string, pv: string[]): string {
  if (!pv.length) return ''
  try {
    const g = new Chess(fen)
    const sans: string[] = []
    for (const uci of pv) {
      const move = uciToMove(uci)
      const result = g.move(move)
      if (!result) {
        sans.push(uci)
        break
      }
      sans.push(result.san)
    }
    return sans.join(' ')
  } catch {
    return pv.join(' ')
  }
}

function formatNodes(n: number | undefined): string {
  if (n == null) return '—'
  if (n >= 1_000_000) return `${(n / 1_000_000).toFixed(2)}M`
  if (n >= 1_000) return `${(n / 1_000).toFixed(1)}k`
  return String(n)
}

function humanColor(mode: PlayMode): 'w' | 'b' | null {
  if (mode === 'human-white') return 'w'
  if (mode === 'human-black') return 'b'
  return null
}

function defaultOrientation(mode: PlayMode): 'white' | 'black' {
  return mode === 'human-black' ? 'black' : 'white'
}

function cursorForHumanTurn(history: Snapshot[], cursor: number, mode: PlayMode): number {
  if (mode === 'analysis' || mode === 'engine-engine') return cursor
  const want = humanColor(mode)
  let c = cursor
  while (c > 0) {
    if (fenSideToMove(history[c].fen) === want) return c
    c -= 1
  }
  return 0
}

function cursorForHumanRedo(history: Snapshot[], cursor: number, mode: PlayMode): number {
  if (mode === 'analysis' || mode === 'engine-engine') {
    return Math.min(cursor + 1, history.length - 1)
  }
  const want = humanColor(mode)
  let c = cursor + 1
  while (c < history.length) {
    if (fenSideToMove(history[c].fen) === want) return c
    c += 1
  }
  return history.length - 1
}

function App() {
  const [pendingMode, setPendingMode] = useState<PlayMode>('human-white')
  const [activeMode, setActiveMode] = useState<PlayMode>('human-white')
  const [history, setHistory] = useState<Snapshot[]>([
    { fen: START_FEN, engine: null, lastMove: null, sans: [], ucis: [] },
  ])
  const [cursor, setCursor] = useState(0)
  const [thinking, setThinking] = useState(false)
  const [engineRunning, setEngineRunning] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [timeMs, setTimeMs] = useState(2000)
  const [threads, setThreads] = useState(1)
  const [skillLevel, setSkillLevel] = useState(20)
  const [useBook, setUseBook] = useState(true)
  const [orientation, setOrientation] = useState<'white' | 'black'>('white')
  const [selectedSquare, setSelectedSquare] = useState<string | null>(null)

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
  const lastMove = snapshot.lastMove
  const game = useMemo(() => gameFromUcis(snapshot.ucis), [snapshot.ucis])
  const player = humanColor(activeMode)

  const canUndo = cursor > 0 && !thinking
  const canRedo = cursor < history.length - 1 && !thinking
  const canInteract =
    !thinking &&
    !game.isGameOver() &&
    activeMode !== 'engine-engine' &&
    (activeMode === 'analysis' || game.turn() === player)

  useEffect(() => {
    setSelectedSquare(null)
  }, [fen, thinking])

  const status = useMemo(() => {
    const terminal =
      outcomeLabel(lastEngine?.gameOver ? lastEngine.outcome : undefined) ?? drawStatusLabel(game)
    if (terminal) return terminal
    if (thinking) {
      return activeMode === 'analysis' ? 'Analyzing…' : 'Engine thinking…'
    }
    if (activeMode === 'engine-engine') {
      return engineRunning ? `${game.turn() === 'w' ? 'White' : 'Black'} to move` : 'Stopped'
    }
    if (game.isCheck()) return 'Check'
    if (activeMode === 'analysis') return `${game.turn() === 'w' ? 'White' : 'Black'} to move`
    return game.turn() === player ? 'Your move' : 'Engine’s turn'
  }, [game, thinking, activeMode, engineRunning, player, lastEngine])

  const moveRows = useMemo(() => pairedMoveRows(snapshot.sans), [snapshot.sans])

  const legalTargets = useMemo(() => {
    if (!selectedSquare || !canInteract) return [] as { to: string; capture: boolean }[]
    return game
      .moves({ square: selectedSquare as never, verbose: true })
      .map((m) => ({ to: m.to, capture: Boolean(m.captured) }))
  }, [selectedSquare, canInteract, game])

  const squareStyles = useMemo(() => {
    const styles: Record<string, CSSProperties> = {}
    if (lastMove) {
      styles[lastMove.from] = { ...LAST_MOVE_STYLE }
      styles[lastMove.to] = { ...LAST_MOVE_STYLE }
    }
    const candidates = lastEngine?.candidates
    if (candidates?.length) {
      candidates.slice(0, 3).forEach((c, i) => {
        if (!c.bestmove || c.bestmove.length < 4) return
        if (!isUciLegal(fen, c.bestmove)) return
        const from = c.bestmove.slice(0, 2)
        const to = c.bestmove.slice(2, 4)
        const style = CANDIDATE_STYLES[i] ?? CANDIDATE_STYLES[2]
        styles[from] = { ...styles[from], ...style }
        styles[to] = { ...styles[to], ...style }
      })
    }
    if (selectedSquare) {
      styles[selectedSquare] = { ...styles[selectedSquare], ...SELECTED_STYLE }
    }
    return styles
  }, [lastMove, selectedSquare, lastEngine, fen])

  const squareRenderer = useCallback(
    ({ square, children }: SquareHandlerArgs & { children?: ReactNode }) => {
      const target = legalTargets.find((t) => t.to === square)
      return (
        <div className="sq" style={squareStyles[square]}>
          {children}
          {target && !target.capture ? <span className="move-dot" aria-hidden /> : null}
          {target?.capture ? <span className="capture-ring" aria-hidden /> : null}
        </div>
      )
    },
    [legalTargets, squareStyles],
  )

  const pushSnapshot = useCallback((next: Snapshot, fromCursor: number) => {
    setHistory((prev) => [...prev.slice(0, fromCursor + 1), next])
    setCursor(fromCursor + 1)
  }, [])

  const applyCursor = useCallback((nextCursor: number) => {
    setCursor(nextCursor)
    setError(null)
    setSelectedSquare(null)
  }, [])

  const fetchBestMove = useCallback(
    async (
      currentFen: string,
      ucis: string[],
      signal: AbortSignal,
      opts?: FetchBestMoveOpts,
    ): Promise<BestMoveResponse> => {
      const payload = JSON.stringify({
        fen: currentFen,
        startFen: START_FEN,
        moves: ucis,
        timeMs,
        threads,
        skillLevel: opts?.skillLevel ?? skillLevel,
        useBook: opts?.useBook ?? useBook,
        multiPv: opts?.multiPv ?? 1,
      })

      const doFetch = async () => {
        const res = await fetch(`${API_URL}/api/bestmove`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          signal,
          body: payload,
        })
        if (!res.ok) {
          const body = await res.text()
          throw new Error(body || `HTTP ${res.status}`)
        }
        return (await res.json()) as BestMoveResponse
      }

      let data: BestMoveResponse
      try {
        data = await doFetch()
      } catch (e) {
        if (signal.aborted) throw e
        // One retry for transient connection drops (keep-alive reuse / brief server blip).
        const msg = e instanceof Error ? e.message : ''
        if (msg === 'Failed to fetch' || e instanceof TypeError) {
          await new Promise((r) => setTimeout(r, 200))
          if (signal.aborted) throw e
          data = await doFetch()
        } else {
          throw e
        }
      }
      return toWhitePov(data, currentFen)
    },
    [timeMs, threads, skillLevel, useBook],
  )

  const requestComputerMove = useCallback(
    async (currentFen: string, fromCursor: number, ucisSoFar: string[], sansSoFar: string[]) => {
      abortRef.current?.abort()
      const controller = new AbortController()
      abortRef.current = controller
      const requestId = ++requestIdRef.current

      setThinking(true)
      setError(null)
      setSelectedSquare(null)
      try {
        const data = await fetchBestMove(currentFen, ucisSoFar, controller.signal)
        if (requestId !== requestIdRef.current) return

        if (data.gameOver) {
          setHistory((prev) => {
            if (fromCursor < 0 || fromCursor >= prev.length) return prev
            const copy = [...prev]
            copy[fromCursor] = { ...copy[fromCursor], engine: data }
            return copy
          })
          setEngineRunning(false)
          return
        }

        const next = gameFromUcis(ucisSoFar)
        const move = uciToMove(data.bestmove)
        const result = next.move(move)
        if (!result) {
          throw new Error(`Engine returned illegal move: ${data.bestmove}`)
        }
        const uci = moveToUci(result)
        pushSnapshot(
          {
            fen: next.fen(),
            engine: data,
            lastMove: { from: result.from, to: result.to },
            sans: [...sansSoFar, result.san],
            ucis: [...ucisSoFar, uci],
          },
          fromCursor,
        )
      } catch (e) {
        if (e instanceof DOMException && e.name === 'AbortError') return
        if (requestId !== requestIdRef.current) return
        const raw = e instanceof Error ? e.message : 'Failed to reach chess server'
        setError(
          raw === 'Failed to fetch'
            ? 'Lost connection to the chess server (is it still running on port 8080?)'
            : raw,
        )
        setEngineRunning(false)
      } finally {
        if (requestId === requestIdRef.current) {
          setThinking(false)
          abortRef.current = null
        }
      }
    },
    [fetchBestMove, pushSnapshot],
  )

  const requestAnalysis = useCallback(
    async (currentFen: string, atCursor: number, ucis: string[]) => {
      abortRef.current?.abort()
      const controller = new AbortController()
      abortRef.current = controller
      const requestId = ++requestIdRef.current

      setThinking(true)
      setError(null)
      try {
        const data = await fetchBestMove(currentFen, ucis, controller.signal, { multiPv: 3 })
        if (requestId !== requestIdRef.current) return
        setHistory((prev) => {
          if (atCursor < 0 || atCursor >= prev.length) return prev
          const copy = [...prev]
          copy[atCursor] = { ...copy[atCursor], engine: data }
          return copy
        })
      } catch (e) {
        if (e instanceof DOMException && e.name === 'AbortError') return
        if (requestId !== requestIdRef.current) return
        const raw = e instanceof Error ? e.message : 'Failed to reach chess server'
        setError(
          raw === 'Failed to fetch'
            ? 'Lost connection to the chess server (is it still running on port 8080?)'
            : raw,
        )
      } finally {
        if (requestId === requestIdRef.current) {
          setThinking(false)
          abortRef.current = null
        }
      }
    },
    [fetchBestMove],
  )

  const requestHint = useCallback(async () => {
    if (thinking || game.isGameOver()) return
    if (activeMode !== 'human-white' && activeMode !== 'human-black') return
    if (game.turn() !== player) return

    abortRef.current?.abort()
    const controller = new AbortController()
    abortRef.current = controller
    const requestId = ++requestIdRef.current
    const atCursor = cursor
    const currentFen = fen
    const ucis = snapshot.ucis

    setThinking(true)
    setError(null)
    try {
      const data = await fetchBestMove(currentFen, ucis, controller.signal, {
        multiPv: 3,
        skillLevel: 20,
        useBook: false,
      })
      if (requestId !== requestIdRef.current) return
      setHistory((prev) => {
        if (atCursor < 0 || atCursor >= prev.length) return prev
        const copy = [...prev]
        copy[atCursor] = { ...copy[atCursor], engine: data }
        return copy
      })
    } catch (e) {
      if (e instanceof DOMException && e.name === 'AbortError') return
      if (requestId !== requestIdRef.current) return
      const raw = e instanceof Error ? e.message : 'Failed to reach chess server'
      setError(
        raw === 'Failed to fetch'
          ? 'Lost connection to the chess server (is it still running on port 8080?)'
          : raw,
      )
    } finally {
      if (requestId === requestIdRef.current) {
        setThinking(false)
        abortRef.current = null
      }
    }
  }, [thinking, game, activeMode, player, cursor, fen, snapshot.ucis, fetchBestMove])

  const playPlayerMove = useCallback(
    (from: string, to: string) => {
      if (!canInteract) return false
      const next = gameFromUcis(snapshot.ucis)
      const move = next.move({ from, to, promotion: 'q' })
      if (!move) return false

      const playerCursor = cursor
      const prevSans = history[playerCursor]?.sans ?? []
      const prevUcis = history[playerCursor]?.ucis ?? []
      const sans = [...prevSans, move.san]
      const ucis = [...prevUcis, moveToUci(move)]
      const nextCursor = playerCursor + 1
      pushSnapshot(
        {
          fen: next.fen(),
          engine: null,
          lastMove: { from: move.from, to: move.to },
          sans,
          ucis,
        },
        playerCursor,
      )
      setSelectedSquare(null)
      setError(null)

      if (next.isGameOver()) return true

      if (activeMode === 'analysis') {
        void requestAnalysis(next.fen(), nextCursor, ucis)
      } else if (activeMode === 'human-white' || activeMode === 'human-black') {
        void requestComputerMove(next.fen(), nextCursor, ucis, sans)
      }
      return true
    },
    [
      canInteract,
      snapshot.ucis,
      cursor,
      history,
      pushSnapshot,
      activeMode,
      requestAnalysis,
      requestComputerMove,
    ],
  )

  const undo = useCallback(() => {
    if (thinking) {
      abortRef.current?.abort()
      abortRef.current = null
      setThinking(false)
      setEngineRunning(false)
      setCursor((c) => Math.max(0, c - 1))
      setError(null)
      setSelectedSquare(null)
      return
    }
    if (cursor <= 0) return
    const next = cursorForHumanTurn(history, cursor - 1, activeMode)
    applyCursor(next)
  }, [thinking, cursor, history, activeMode, applyCursor])

  const redo = useCallback(() => {
    if (thinking || cursor >= history.length - 1) return
    const next = cursorForHumanRedo(history, cursor, activeMode)
    applyCursor(next)
  }, [thinking, cursor, history, activeMode, applyCursor])

  const trySelectOrMove = useCallback(
    (square: string) => {
      if (!canInteract) {
        setSelectedSquare(null)
        return
      }

      if (selectedSquare) {
        if (legalTargets.some((t) => t.to === square)) {
          playPlayerMove(selectedSquare, square)
          return
        }
      }

      const piece = game.get(square as never)
      const allowedColor =
        activeMode === 'analysis' ? game.turn() : player
      if (piece && allowedColor && piece.color === allowedColor) {
        setSelectedSquare((prev) => (prev === square ? null : square))
        return
      }

      setSelectedSquare(null)
    },
    [canInteract, selectedSquare, legalTargets, game, playPlayerMove, activeMode, player],
  )

  const onPieceDrop = useCallback(
    ({ sourceSquare, targetSquare }: PieceDropHandlerArgs) => {
      if (!targetSquare) return false
      return playPlayerMove(sourceSquare, targetSquare)
    },
    [playPlayerMove],
  )

  const onSquareClick = useCallback(
    ({ square }: SquareHandlerArgs) => {
      trySelectOrMove(square)
    },
    [trySelectOrMove],
  )

  const onPieceClick = useCallback(
    ({ square }: PieceHandlerArgs) => {
      if (!square) return
      trySelectOrMove(square)
    },
    [trySelectOrMove],
  )

  const onPieceDrag = useCallback(
    ({ square }: PieceHandlerArgs) => {
      if (!canInteract || !square) return
      const piece = game.get(square as never)
      const allowedColor = activeMode === 'analysis' ? game.turn() : player
      if (piece && allowedColor && piece.color === allowedColor) setSelectedSquare(square)
    },
    [canInteract, game, activeMode, player],
  )

  const stopEngine = useCallback(() => {
    abortRef.current?.abort()
    abortRef.current = null
    requestIdRef.current += 1
    setThinking(false)
    setEngineRunning(false)
  }, [])

  const newGame = useCallback(() => {
    stopEngine()
    const mode = pendingMode
    setActiveMode(mode)
    setOrientation(defaultOrientation(mode))
    setHistory([{ fen: START_FEN, engine: null, lastMove: null, sans: [], ucis: [] }])
    setCursor(0)
    setError(null)
    setSelectedSquare(null)

    if (mode === 'engine-engine') {
      setEngineRunning(true)
    } else if (mode === 'human-black') {
      void requestComputerMove(START_FEN, 0, [], [])
    } else if (mode === 'analysis') {
      void requestAnalysis(START_FEN, 0, [])
    }
  }, [pendingMode, stopEngine, requestComputerMove, requestAnalysis])

  // Engine vs engine auto-play loop
  useEffect(() => {
    if (activeMode !== 'engine-engine' || !engineRunning || thinking) return
    const g = gameFromUcis(snapshot.ucis)
    if (g.isGameOver()) {
      setEngineRunning(false)
      return
    }
    void requestComputerMove(fen, cursor, snapshot.ucis, snapshot.sans)
  }, [
    activeMode,
    engineRunning,
    thinking,
    fen,
    cursor,
    snapshot.ucis,
    snapshot.sans,
    requestComputerMove,
  ])

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
      allowDragging: canInteract,
      squareStyles,
      squareRenderer,
      onPieceDrop,
      onSquareClick,
      onPieceClick,
      onPieceDrag,
    }),
    [
      fen,
      orientation,
      canInteract,
      squareStyles,
      squareRenderer,
      onPieceDrop,
      onSquareClick,
      onPieceClick,
      onPieceDrag,
    ],
  )

  const searchFenForPv = useMemo(() => {
    // Analysis / hint: engine lines are for the current fen.
    // After the engine moves in play, lines are for the previous fen.
    if (!lastEngine?.bestmove) return fen
    if (activeMode === 'analysis') return fen
    if (isUciLegal(fen, lastEngine.bestmove)) return fen
    if (lastMove && cursor > 0) return history[cursor - 1]?.fen ?? fen
    return fen
  }, [lastEngine, fen, activeMode, lastMove, cursor, history])

  const candidateLines = useMemo(() => {
    if (!lastEngine) return [] as CandidateLine[]
    if (lastEngine.candidates?.length) return lastEngine.candidates
    return [
      {
        bestmove: lastEngine.bestmove,
        scoreCp: lastEngine.scoreCp,
        scoreMate: lastEngine.scoreMate,
        pv: lastEngine.pv,
      },
    ]
  }, [lastEngine])

  const modePending = pendingMode !== activeMode
  const barPct = evalBarPercent(lastEngine)
  const canHint =
    (activeMode === 'human-white' || activeMode === 'human-black') &&
    !thinking &&
    !game.isGameOver() &&
    game.turn() === player

  return (
    <div className="layout">
      <header className="brand">
        <div>
          <h1>Chess AI</h1>
          <p>{PLAY_MODE_LABELS[activeMode]} · native C++ engine</p>
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
            <button
              type="button"
              className="btn btn-ghost"
              onClick={() => setOrientation((o) => (o === 'white' ? 'black' : 'white'))}
            >
              Flip
            </button>
            {(activeMode === 'human-white' || activeMode === 'human-black') && (
              <button
                type="button"
                className="btn btn-ghost"
                onClick={() => void requestHint()}
                disabled={!canHint}
              >
                Hint
              </button>
            )}
            {activeMode === 'engine-engine' && engineRunning ? (
              <button type="button" className="btn btn-ghost" onClick={stopEngine}>
                Stop
              </button>
            ) : null}
            {activeMode === 'engine-engine' && !engineRunning && !game.isGameOver() ? (
              <button
                type="button"
                className="btn btn-ghost"
                disabled={thinking}
                onClick={() => setEngineRunning(true)}
              >
                Resume
              </button>
            ) : null}
            <button type="button" className="btn btn-primary" onClick={newGame}>
              New game
            </button>
          </div>
          <p className="hint">⌘/Ctrl+Z undo · ⌘/Ctrl+Shift+Z redo · click a piece to see moves</p>
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

          <section className="insights" aria-label="Engine insights">
            <div className="eval-bar" title="Evaluation from White’s perspective">
              <div className="eval-bar-fill" style={{ width: `${barPct}%` }} />
            </div>

            {lastEngine ? (
              <>
                <div className="insights-head">
                  <span className="eval-score">{formatEval(lastEngine)}</span>
                  {lastEngine.fromBook ? (
                    <span className="badge">book</span>
                  ) : (
                    <span className="insights-depth">depth {lastEngine.depth}</span>
                  )}
                </div>
                {!lastEngine.fromBook && (
                  <div className="insights-stats">
                    <span>{formatNodes(lastEngine.nodes)} nodes</span>
                    <span>{formatNodes(lastEngine.nps)} nps</span>
                    <span>{lastEngine.timeMs ?? 0} ms</span>
                  </div>
                )}
                {candidateLines.length > 0 && (
                  <ol className="insights-candidates">
                    {candidateLines.slice(0, 3).map((line, i) => {
                      const pv = line.pv?.length ? line.pv : [line.bestmove]
                      const san = pvToSan(searchFenForPv, pv) || line.bestmove
                      return (
                        <li key={`${line.bestmove}-${i}`}>
                          <span className="cand-rank">{i + 1}.</span>
                          <span className="cand-eval">
                            {lastEngine.fromBook && i === 0 ? 'Book' : formatEval(line)}
                          </span>
                          <span className="cand-pv">{san}</span>
                        </li>
                      )
                    })}
                  </ol>
                )}
              </>
            ) : (
              <p className="insights-empty">
                {thinking
                  ? 'Waiting for search…'
                  : 'Engine insights appear after a search. Use Hint for top moves.'}
              </p>
            )}
          </section>

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
              Mode
              <select
                className="mode-select"
                value={pendingMode}
                disabled={thinking && activeMode === 'engine-engine' && engineRunning}
                onChange={(e) => setPendingMode(e.target.value as PlayMode)}
              >
                {(Object.keys(PLAY_MODE_LABELS) as PlayMode[]).map((m) => (
                  <option key={m} value={m}>
                    {PLAY_MODE_LABELS[m]}
                  </option>
                ))}
              </select>
              {modePending ? (
                <span className="mode-hint">Applies on New game</span>
              ) : null}
            </label>

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
