# Interview revision: portfolio at a glance (~2 pages)

This note maps **what you built** to **libraries, patterns, and crisp interview lines**. It is organized for quick recall before technical discussions.

---

## Central table (revise this first)

| Domain | Where in your tree | Stack / libraries | One-line “headline” |
|--------|--------------------|--------------------|----------------------|
| **Meta-optimization & solvers** | `agentic_ai_projects/langgraph/meta_optimizer` | **LangGraph**, Pydantic, **Optuna** (TPE, CMA-ES, Random, QMC; Median/Hyperband/SHA pruners), **SciPy** (`minimize` / SLSQP, **`dual_annealing`**), **DEAP**, **Google OR-Tools CP-SAT** (2D bin packing MVP), **`gurobipy`** in deps (commercial LP/MIP hook), MCP | LLM **classifies the problem**, picks a **strategy**, routes to the right **numeric solver**; black-box objectives evaluated safely. |
| **Genetic algorithms (hands-on)** | `optimizations/` | **DEAP** (`creator`, `Toolbox`, `eaSimple`), NumPy, Pandas | Classic GA patterns: tournaments, Gaussian / uniform mutation, crossover, hall-of-fame, logbook stats. |
| **GA + symbolic / creative** | `optimizations/symbolic_regression_genetic.py`, `image_approx_deap_genetic.py` | DEAP | Evolving representations (trees / parameters) toward a fitness target—not just tuning floats. |
| **Simulated annealing** | `optimizations/simulated_annealing.ipynb`, routed in meta_optimizer | **SciPy `dual_annealing`** (global + optional local polish) | For **rugged / multi-modal** objectives; tempering schedule explores then refines minima. |
| **Routing (VLSI-style)** | `optimizations/routing_deap_rustworkx_astar.py`, `routing_deap_networkx_astar.py`, `routing_deap_graph_tool_astar.py` | **rustworkx** / **NetworkX** / **graph-tool**, DEAP on **pin order** & **net order**, shortest-path style costs | Grid graph construction → **combinatorial sequencing** via GA → **astar / dijkstra**-style routing on weighted graphs; separates “order” vs “path”. |
| **Routing architecture (design)** | `eda_scratchpad/vlsi_router/docs/` | (design) DEAP + Optuna + A* concepts | Documents **when** GA/Optuna vs **deterministic** maze routing/DRC—not everything is lifted to meta-optimization. |
| **Probabilistic / “Bayesian-flavored” reasoning** | `crewai/portfolio_manager` (docs + tools) | **SciPy.stats** normal CDF/PPF for probs & CVaR-style tails; probabilistic tooling layers on rules | Moves from deterministic rules → **confidence**, **probability of upside**, downside quantiles; calibrated language for risk (full PyMC Bayesian stack not emphasized in codebase—know the distinction vs **proper** Bayesian inference). |
| **Stock / factor scoring** | `langgraph/langgraph_stock_analyzer`, `langgraph/covered_call_analyzer` | YAML sector weights, multi-pillar scoring, **Pydantic** validation gates | **Single source of truth** in Python: gates + enums; LLM constrained to rationale; “decide once, render everywhere.” |
| **RL + options automation** | `llm_reinforced_learning/FinSightTrader` | **Gymnasium**, **Stable-Baselines3 (PPO)**, LangGraph orchestration, Alpaca/YFinance/FMP connectors | Discrete **policy actions** (`skip`, size buckets) paired with deterministic risk caps and paper execution. |
| **Agentic pipelines** | CrewAI folders, LangGraph graphs | CrewAI / LangChain providers, SSE, FastAPI | Multi-step workflows: screen → score → gate → rank → notify / trade. |
| **RAG over your codebase** | `mcp_servers/mcp_myDocs_RAG` | **ChromaDB**, OpenAI-compat client → **Ollama** embeddings + summarization | Local retrieval augmented generation for “what did I implement where?” |

---

## Bayes / probability (how to phrase it technically)

You use **explicit uncertainty**, not necessarily full hierarchical Bayes everywhere.

- **What you have**: Gaussian-style tail metrics (`norm.cdf` / `ppf`) for *prob_positive_return*, downside at 5%, etc.—i.e. **closed-form probabilistic reasoning** layered on heuristic or factor models.
- **Interview angle**: distinguish **calibrated probabilistic forecasting** vs **posterior inference** (MCMC): you can honestly say “we quantify uncertainty via parametric tails and confidence bands tied to factor agreement”; if asked for Bayesian updating, cite **portfolio feedback trackers** / future **joblib-sklearn or PyMC** as natural extensions you’ve scaffolded docs for—not as completed PyMC pipelines unless you deployed them.

---

## Genetic algorithms (DEAP)—soundbites

- **Chromosome**: vector (floats / ints per problem), or permutation indices for routing order.
- **Operators**: crossover (e.g. `cxBlend`, `cxTwoPoint`), mutation (`mutGaussian`, `mutUniformInt`), selection (`selTournament`), optional **HallOfFame** to preserve historical best.
- **Loop**: evaluate → select → mate → mutate → replace; logbook tracks convergence.
- **meta_optimizer specialization**: floats + `cxBlend` + `mutGaussian` on bounded continuous problems with a black-box evaluator.

---

## Simulated / dual annealing

- **Dual annealing** (SciPy): combines **slow cooling** random search with optional **local minimization**; good when the landscape is multi-modal (“rugged”).
- Tie to **workflow**: meta_optimizer heuristic routes keywords like “rugged/multi-modal” → `dual_annealing` hyperparameters (`maxiter`).

---

## Routing + solvers (layered architecture)

Memorize the **division of labour**:

| Layer | Typical algorithm | Question it answers |
|-------|-------------------|---------------------|
| **Graph build** | Custom grid → edges/weights | “What geometry is routable?” |
| **Combinatorial ordering** | **DEAP GA** on permutations | “In what order should I rip/reroute nets or pins?” |
| **Actual pathfinding** | **rustworkx** `astar_*` / `dijkstra_*` (similar for NetworkX) | “Minimum-cost path given an order?” |
| **Higher-level orchestration** | Optuna NSGA-II / tuning in design docs | “When congestion penalties or multi-objective trade-offs dominate?” |

**One memorable line**: *“Genetic algorithms search **discrete orderings** where the exhaustive space is factorial; deterministic shortest-path solvers honor each ordering on a weighted graph.”*

---

## Meta-optimizer solver menu (elevator recap)

When an interviewer asks “what solvers?” list **by problem class**:

- **Convex-ish NLP with constraints**: SciPy **`minimize` (e.g. SLSQP)**.
- **Global black-box rugged**: **`dual_annealing`** or Optuna **`TPESampler` / `CmaEsSampler`** trials.
- **Discrete packing/CSP MVP**: OR-Tools **`CpModel`** with optional intervals and no-overlap.
- **Population-based discrete/continuous hybrids**: DEAP **`eaSimple`**-style loops (custom in meta_optimizer for continuous bounding).
- **Commercial MIP**: `gurobipy` listed—position as **integrated option** where licenses exist.

---

## Agent + trading / validation themes

- **Pydantic + gates**: programmatic verdict/action-zone consistency beats LLM hallucination in production paths.
- **RL (SB3/PPO)**: learns **when** to allocate size conditional on observations; deterministic **risk** (caps, limits, kill-switch) wraps the policy.
- **Operational lesson you lived**: separating **estimated IVRank** proxies from trade quality; caches and floors matter for automation.

---

## Quick cram checklist (say these out loud once)

1. **DEAP**: population, toolbox, crossover/mutation probs, tournament selection, HoF.
2. **Dual annealing**: global stochastic search + optional local refinement.
3. **Optuna**: study direction, TPE/CMA/QMC sampler, enqueue warm-start trials.
4. **CP-SAT**: Boolean/IntVars, optional intervals for non-overlap, `CpSolver`.
5. **Routing**: GA on **ordering**, graph library on **path**; factorial vs polynomial split.
6. **Probabilistic layer**: normals for tail risk—name it “parametric uncertainty” unless you cite real MCMC.

---

## Plain-language guide: what each method does (+ when to choose it)

The paragraphs below avoid jargon. Then you get a **speed ranking** (slowest first). *Speed depends on problem size and how costly each objective call is—the list is a useful default when your code runs a simulator, portfolio check, or route cost many times over.*

---

### Reinforcement learning (e.g. PPO in Stable-Baselines3)

**Goal:** Teach an agent **which discrete action** to take (trade size, skip, etc.) from repeated **try → reward → try again** loops, instead of writing every rule by hand.

**How it works:** The environment tells the policy “what happened” after each step. The learner nudges the policy toward actions that yielded higher long-run reward using gradient-style updates.

**Best when:** The problem feels like **sequential decisions under uncertainty**, you can define rewards, and logging lots of simulated experience is acceptable. Prefer **explicit optimizers or rules** when you already have clean math and constraints or when training data is unrealistic.

---

### Genetic algorithms (DEAP)

**Goal:** Move a **population** of candidate solutions toward better ones **without** needing derivatives.

**How it works:** Score each candidate (fitness); keep stronger ones more often (selection); **mix pairs** (crossover); **lightly randomize** (mutation); repeat for many generations.

**Best when:** The search space is **messy**, **combinatorial** (routes, ordering), or the objective is expensive but you can afford **many evaluations**. Prefer **deterministic MILP/CSP or gradient methods** when the problem is linear, convex, or you have exact constraints in equation form—those are usually faster and more repeatable.

---

### Dual annealing (SciPy)

**Goal:** Escape **local dips** on a rugged surface and settle near a global low point.

**How it works:** It takes **random exploratory steps** whose “accept chance” gradually tightens—like controlling temperature in physical annealing—then can **polish locally** once a good basin is found.

**Best when:** You have **low-to-medium dimension**, **black-box**, **multi-modal** objectives and no natural gradient (or no good starting point). Prefer **fewer-shot tuners like Optuna** if you tune many hyperparameters in parallel workflows, or **GA** when permutations/orderings—not smooth vectors—are the real degrees of freedom.

---

### Hyperparameter-style search — Optuna (TPE / CMA-ES / trials)

**Goal:** Decide **numeric or categorical knobs** trial by trial **without** building a differentiable model of the objective.

**How it works:** Each trial suggests parameters; outcomes get recorded; the **sampler learns** where higher scores tend to lie (Tree-Structured Parzen for TPE, etc.), so later trials skew toward promising regions **sample-efficient**.

**Best when:** You optimize **several knobs** whose interaction is opaque, optionally with **early stopping/pruners**. It is typically **fewer brute-force trials than exhaustive grid**. Prefer **SLSQP** if constraints are analytic; prefer **GA** when the structure is factorial permutations dominated.

---

### Local constrained optimization — SciPy `minimize` (e.g. SLSQP)

**Goal:** Find a **smooth-ish** optimum **near a starting point**, honoring simple bounds/equalities/inequalities you can write as formulas.

**How it works:** It uses calculus-style **local steps** influenced by gradients (finite-difference approximations allowed) projected through your constraints until stopping rules fire.

**Best when:** The model is mostly **smooth**, constraints are explicit, dimensions are modest, you have at least **one reasonable seed**. Prefer **dual annealing or GA** when the objective is chaotic or you routinely land in dead-end local minima from bad seeds.

---

### Discrete feasibility & packing — Google OR-Tools CP-SAT

**Goal:** Find **YES/NO and integer schedules** placing items in bins obeying geometric or logical constraints (no overlap, at most once, totals match).

**How it works:** The problem becomes **Boolean/int logic** clauses in a SAT-style engine with strong **domain propagation**. The solver chops huge search branches away—without random evolution.

**Best when:** The task is inherently **combinatorial with clear rules**. This is usually **much faster than GA** for comparable structured instances. Prefer **continuous relaxations + SLSQP** if geometry is nicer as smooth math; GA when you cannot formulate clean CP constraints.

---

### Commercial MILP/MQP — Gurobi (concept)

**Goal:** Solve **mixed-integer linear** (or quadratic) models with industrial-grade branching and cuts.

**How it works:** Branch-and-bound/tree search with optimizations not fully exposed—but you model **constraints + objectives** and call solve.

**Best when:** The business problem **already sits** as a MILP/QP, you have a license, and you need **provable optimality or tight bounds** at scale. Overkill for quick prototypes with tiny models.

---

### Shortest path on a fixed graph — Dijkstra / A*

**Goal:** One **cheapest route** from A to B on a known weighted graph.

**How it works:** Explore edges in order of **promising partial cost** (Dijkstra) or add a **heuristic toward the goal** (A*) to visit fewer nodes.

**Best when:** The graph is fixed and costs are clear. In **routing**, this is the **fast inner step** after something else (often GA) picks an order. By itself it does **not** solve **which order to route nets**—that’s where slower search appears.

---

### Parametric probability (`scipy.stats.norm`, etc.)

**Not an optimizer:** it **converts** means and standard deviations into **probabilities** or **tail cutoffs** in closed form.

**Best when:** You want a **transparent story** about upside/downside under a simple distribution. **Not** a substitute for full Bayesian updating when you need posteriors over many coupled parameters.

---

### Pydantic gates (validation)

**Not an optimizer:** it **enforces invariants** on structured data so automated systems stay consistent.

**Best when:** LLMs or external APIs feed **prose or JSON** and you need **final truth** in code.

---

### RAG retrieval (vectors + prompts)

**Not an optimizer:** it **finds** similar text chunks **fast** compared to naive full-file scans, then hands them to a model.

**Best when:** Your knowledge sits in docs/code and questions are **messy wording** needing context.

---

## Speed ranking — optimisation-style methods only (slowest → fastest)

*Reminder: RL training dominates wall time versus “solve once” methods; MILP beats random search only when formulation matches the problem.*

| Rank | Method | Typical feel |
|:---:|:---|:---|
| 1 | **RL training** (many env steps until policy learns) | Often **minutes to hours**. |
| 2 | **Genetic algorithms** (population × generations × fitness) | **Very slow** if each fitness is routing or simulator. |
| 3 | **Dual annealing** (many global + local evaluations) | **Slow** sequential black-box probing. |
| 4 | **Optuna heavy study** (`n_trials` large, pruning off) | **Medium**—fewer pointless trials than random grid, still depends on trials. |
| 5 | **SciPy local `minimize`** (bounded smooth problems) | **Medium-to-fast** with good seed—many fewer steps than evolutionary search. |
| 6 | **CP-SAT / structured discrete solve** one shot | Usually **fast** once modeled—solver exploits structure. |
| 7 | **Commercial MILP** (fits model class, good license) | **Very fast-to-fast** relative to stochastic black-box loops at production scale—**modeling burden** dominates human time more than runtime. |
| 8 | **Single-source shortest path** (Dijkstra / A*, right-sized graph) | **Very fast**: near-linear in vertices/edges practically. |

*Hybrid routing stack:* **GA reordering dominates** runtime; shortest-path phases are comparatively cheap.

---

## Tiny code snippets (easy mental models)

Each block is deliberately small: read the imports plus the lines marked with comments that say what moves.

### 1. Genetic algorithm (DEAP) — evolve a vector to minimize sphere(x)

Evolution cycle: evaluate → tournament select → crossover → Gaussian mutation → next generation.

```python
from deap import base, creator, tools, algorithms
import random

creator.create("FitnessMin", base.Fitness, weights=(-1.0,))  # minimize
creator.create("Individual", list, fitness=creator.FitnessMin)

toolbox = base.Toolbox()
toolbox.register("gene", random.uniform, -5.0, 5.0)
toolbox.register("individual", tools.initRepeat, creator.Individual, toolbox.gene, n=3)
toolbox.register("population", tools.initRepeat, list, toolbox.individual)

def fitness_sphere(individual):
    return (sum(g * g for g in individual),)  # f(x)=||x||^2

toolbox.register("evaluate", fitness_sphere)
toolbox.register("mate", tools.cxBlend, alpha=0.5)
toolbox.register("mutate", tools.mutGaussian, mu=0.0, sigma=0.5, indpb=0.25)
toolbox.register("select", tools.selTournament, tournsize=3)

pop = toolbox.population(n=30)
hof = tools.HallOfFame(1)
algorithms.eaSimple(pop, toolbox, cxpb=0.6, mutpb=0.3, ngen=15, halloffame=hof, verbose=False)
# best specimen:
print(list(hof[0]), "fitness=", hof[0].fitness.values[0])
```

### 2. Simulated-annealing style global search — SciPy `dual_annealing`

Good for a bumpy 1D function with many local minima.

```python
import numpy as np
from scipy.optimize import dual_annealing

def rastrigin_1d(x):
    x = x[0]
    return x**2 - 10 * np.cos(2 * np.pi * x) + 10

res = dual_annealing(rastrigin_1d, bounds=[(-5.0, 5.0)], maxiter=200)
print("x =", res.x, "f =", res.fun)
```

### 3. Constrained local NLP — SciPy `minimize` (SLSQP)

Small quadratic with a linear inequality `x0 + x1 <= 1`.

```python
import numpy as np
from scipy.optimize import minimize

def obj(x):
    return x[0]**2 + x[1]**2

cons = ({"type": "ineq", "fun": lambda x: 1.0 - x[0] - x[1]},)  # x0+x1 <= 1
res = minimize(obj, x0=[0.5, 0.5], method="SLSQP", bounds=[(0, 1), (0, 1)], constraints=cons)
print(res.x, res.fun)
```

### 4. Bayesian optimization–style tuning — Optuna finding `a,b` that minimize `(a-2)^2 + (b+1)^2`

TPE proposes smart trial points without hand-written gradients.

```python
import optuna

def objective(trial):
    a = trial.suggest_float("a", -5.0, 5.0)
    b = trial.suggest_float("b", -5.0, 5.0)
    return (a - 2.0)**2 + (b + 1.0)**2

study = optuna.create_study(direction="minimize")
study.optimize(objective, n_trials=40, show_progress_bar=False)
print("best:", study.best_params, "value=", study.best_value)
```

### 5. Discrete constraint solver — OR-Tools CP-SAT (“pick smallest subset that sums ≥ target”)

Pattern: BoolVars + linear constraint + minimize sum of selections.

```python
from ortools.sat.python import cp_model

weights = [3, 5, 2, 7]
target = 10

model = cp_model.CpModel()
pick = [model.NewBoolVar(f"x{i}") for i in range(len(weights))]
model.Add(sum(pick[i] * weights[i] for i in range(len(weights))) >= target)
model.Minimize(sum(pick))

solver = cp_model.CpSolver()
solver.Solve(model)
chosen = [i for i, v in enumerate(pick) if solver.Value(v) == 1]
print("chosen indices", chosen, "cost", solver.ObjectiveValue())
```

### 6. Parametric uncertainty (not MCMC) — normal tail probability

“What is P(return > 0) if returns ~ N(mu, sigma)?” Uses the same statistical idea as `scipy.stats.norm` in forecasting tools.

```python
from scipy.stats import norm

mu, sigma = 0.02, 0.05  # e.g. monthly return mean & stdev
prob_positive = 1.0 - norm.cdf(0.0, loc=mu, scale=sigma)
var_95 = norm.ppf(0.05, loc=mu, scale=sigma)  # 5% left tail of return
print("P(R>0)=", prob_positive, " 5% VaR edge ~", var_95)
```

### 7. Routing split — permute *order*, then shortest path on a graph

Your real code uses grid graphs + rustworkx; this is the same **idea** in five lines with NetworkX.

```python
import networkx as nx

G = nx.Graph()
G.add_edge("A", "B", weight=2)
G.add_edge("B", "C", weight=1)
G.add_edge("A", "C", weight=5)

order = ["A", "B", "C"]  # GA could search permutations of jobs to visit
cost = sum(
    nx.shortest_path_length(G, order[i], order[i + 1], weight="weight")
    for i in range(len(order) - 1)
)
print("visit order", order, "total cost", cost)
```

### 8. Validation “single source of truth” — Pydantic `model_validator`

Python enforces invariants; LLM output can be merged then coerced.

```python
from enum import Enum
from pydantic import BaseModel, model_validator

class Zone(str, Enum):
    HOLD = "hold_only"
    ACTIVE = "active"

class Signal(BaseModel):
    zone: Zone
    buy_ok: bool

    @model_validator(mode="after")
    def no_buy_in_hold(self):
        if self.zone == Zone.HOLD and self.buy_ok:
            self.buy_ok = False  # force consistent state
        return self

print(Signal(zone=Zone.HOLD, buy_ok=True))  # buy_ok becomes False
```

### 9. RL shape — Gymnasium `reset` / `step` + discrete action

Stable-Baselines3 wraps this; the **contract** is observation, action, reward, terminated, truncated, info.

```python
import gymnasium as gym

env = gym.make("CartPole-v1")
obs, _ = env.reset(seed=0)
action = env.action_space.sample()  # 0 or 1
obs, reward, terminated, truncated, info = env.step(action)
print("obs dim", len(obs), "reward", reward, "done", terminated or truncated)
env.close()
```

### 10. RAG shape — embed query, retrieve top-k, stuff into prompt (conceptual)

Chroma abstracts storage; mentally: **vectors in, ids + documents out.**

```python
# Pseudocode — API names vary by chromadb version

# collection.add(embeddings=[...], documents=["def foo(): ..."], ids=["foo.py:1"])

# query_embedding = embed("Where is simulated annealing used?")

# hits = collection.query(query_embeddings=[query_embedding], n_results=3)

# prompt = system + "".join(hits["documents"][0])

# reply = llm.complete(prompt)

```

---

*Generated for interview prep from your active `python_programs` corpus; extend with `cpp_programs/` or `security/` if those subtrees gain substantial code you want summarized.*
