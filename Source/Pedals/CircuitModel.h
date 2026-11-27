#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

//==============================================================================
/** Circuito a nodi con op-amp ideali, discretizzato con la regola del trapezio (metodo nodale DK:
    Yeh, Abel, Smith 2010; Holters, Zölzer 2015), con al più una porta non lineare: una coppia di
    diodi antiparalleli tra un nodo e la massa.

    NC condensatori, NU ingressi, NY uscite: fissi a compilazione, così il calcolo per campione
    (solo prodotti matrice-vettore) viene srotolato dal compilatore.

    Il nodo 0 è la massa del segnale (Vb nel pedale vero). Gli ingressi sono sorgenti di tensione
    ideali verso massa; le uscite sono tensioni di nodo.

    Uso: si descrive il circuito (add...), poi prepare() alloca tutto. Dopo prepare() i valori delle
    resistenze si cambiano con setResistance() e update() ricalcola le matrici senza allocare: si
    può chiamare dal thread audio quando gira una manopola. */
template <int NC, int NU, int NY>
class CircuitModel
{
public:
    //==============================================================================
    // Descrizione (prima di prepare)
    int addNode()                                       { return ++numNodes; }
    int addResistor (int a, int b, double ohms)         { resistors.push_back ({ a, b, ohms }); return (int) resistors.size() - 1; }
    void addCapacitor (int a, int b, double farads)     { capacitors.push_back ({ a, b, farads }); }
    void addInput (int node)                            { inputs.push_back (node); }
    void addOpAmp (int inverting, int nonInverting, int output) { opAmps.push_back ({ inverting, nonInverting, output }); }
    void addOutput (int node)                           { outputs.push_back (node); }

    /** Coppia di diodi uguali antiparalleli tra node e massa: i = 2 Is sinh (v / (n Vt)). */
    void setDiodePair (int node, double saturationCurrent, double emission, double thermalVoltage = 0.02585)
    {
        diodeNode = node;
        diodeIs = saturationCurrent;
        diodeVt = emission * thermalVoltage;
    }

    //==============================================================================
    /** Alloca e calcola le matrici. Thread dell'interfaccia o prepareToPlay. */
    void prepare (double sampleRate)
    {
        jassert ((int) capacitors.size() == NC && (int) inputs.size() == NU && (int) outputs.size() == NY);

        size = numNodes + NU + (int) opAmps.size();
        S.assign ((size_t) (size * size), 0.0);
        P.assign ((size_t) (size * size), 0.0);

        for (int c = 0; c < NC; ++c)
            gc[(size_t) c] = 2.0 * capacitors[(size_t) c].farads * sampleRate;

        dirty = true;
        update();
        reset();
    }

    void reset()
    {
        h.fill (0.0);
        vDiode = pLast = 0.0;
        slopeLast = 1.0;
    }

    void setResistance (int index, double ohms)
    {
        auto& r = resistors[(size_t) index];
        if (! juce::exactlyEqual (r.ohms, ohms))
        {
            r.ohms = ohms;
            dirty = true;
        }
    }

    /** Ricalcola le matrici se qualche resistenza è cambiata. Senza allocazioni. */
    void update()
    {
        if (! dirty || size == 0)
            return;
        dirty = false;

        std::fill (S.begin(), S.end(), 0.0);
        const auto stamp = [this] (int a, int b, double g)
        {
            if (a > 0) at (S, a - 1, a - 1) += g;
            if (b > 0) at (S, b - 1, b - 1) += g;
            if (a > 0 && b > 0) { at (S, a - 1, b - 1) -= g; at (S, b - 1, a - 1) -= g; }
        };

        for (auto& r : resistors)
            stamp (r.a, r.b, 1.0 / r.ohms);
        for (int c = 0; c < NC; ++c)
            stamp (capacitors[(size_t) c].a, capacitors[(size_t) c].b, gc[(size_t) c]);

        int k = numNodes;
        for (auto node : inputs)   // sorgente di tensione: corrente incognita nella colonna k
        {
            at (S, node - 1, k) += 1.0;
            at (S, k, node - 1) += 1.0;
            ++k;
        }
        for (auto& op : opAmps)    // nullor: corrente d'uscita libera, v(+) = v(-)
        {
            if (op.out > 0) at (S, op.out - 1, k) += 1.0;
            if (op.nonInv > 0) at (S, k, op.nonInv - 1) += 1.0;
            if (op.inv > 0) at (S, k, op.inv - 1) -= 1.0;
            ++k;
        }

        invert();

        // Le matrici del sistema a tempo discreto sono combinazioni di elementi di P = S^-1.
        // Le righe dei condensatori sono già moltiplicate per 2 gc (aggiornamento dello stato).
        const auto pv = [this] (int rowNode, int colNode)   // P tra due nodi (0 = massa)
        {
            return (rowNode > 0 && colNode > 0) ? at (P, rowNode - 1, colNode - 1) : 0.0;
        };
        const auto capRow = [&] (int c, int col)              // N_x P e_col
        {
            const auto& cap = capacitors[(size_t) c];
            return (cap.a > 0 ? at (P, cap.a - 1, col) : 0.0) - (cap.b > 0 ? at (P, cap.b - 1, col) : 0.0);
        };
        const auto nodeCap = [&] (int node, int d)            // e_node P N_x^T
        {
            const auto& cd = capacitors[(size_t) d];
            return pv (node, cd.a) - pv (node, cd.b);
        };

        for (int c = 0; c < NC; ++c)
        {
            const double scale = 2.0 * gc[(size_t) c];
            for (int d = 0; d < NC; ++d)
            {
                const auto& cd = capacitors[(size_t) d];
                Mxx[(size_t) c][(size_t) d] = scale * ((cd.a > 0 ? capRow (c, cd.a - 1) : 0.0) - (cd.b > 0 ? capRow (c, cd.b - 1) : 0.0))
                                              - (c == d ? 1.0 : 0.0);
            }
            for (int u = 0; u < NU; ++u)
                Mxu[(size_t) c][(size_t) u] = scale * capRow (c, numNodes + u);
            Mxn[(size_t) c] = diodeNode > 0 ? scale * capRow (c, diodeNode - 1) : 0.0;
        }

        if (diodeNode > 0)
        {
            for (int d = 0; d < NC; ++d)
                Mnx[(size_t) d] = nodeCap (diodeNode, d);
            for (int u = 0; u < NU; ++u)
                Mnu[(size_t) u] = at (P, diodeNode - 1, numNodes + u);
            K = at (P, diodeNode - 1, diodeNode - 1);
        }

        for (int y = 0; y < NY; ++y)
        {
            const int node = outputs[(size_t) y];
            for (int d = 0; d < NC; ++d)
                Myx[(size_t) y][(size_t) d] = nodeCap (node, d);
            for (int u = 0; u < NU; ++u)
                Myu[(size_t) y][(size_t) u] = at (P, node - 1, numNodes + u);
            Myn[(size_t) y] = diodeNode > 0 ? pv (node, diodeNode) : 0.0;
        }
    }

    //==============================================================================
    /** Un campione: in = tensioni degli ingressi, out = tensioni delle uscite. Thread audio. */
    void process (const double* in, double* out) noexcept
    {
        double i = 0.0;

        if (diodeNode > 0)
        {
            double p = 0.0;
            for (size_t d = 0; d < NC; ++d) p += Mnx[d] * h[d];
            for (size_t u = 0; u < NU; ++u) p += Mnu[u] * in[u];
            i = solveDiode (p);
        }

        for (size_t y = 0; y < NY; ++y)
        {
            double v = Myn[y] * i;
            for (size_t d = 0; d < NC; ++d) v += Myx[y][d] * h[d];
            for (size_t u = 0; u < NU; ++u) v += Myu[y][u] * in[u];
            out[y] = v;
        }

        // regola del trapezio: h' = 2 gc v_c - h, con 2 gc e -h già dentro le matrici
        std::array<double, NC> next;
        for (size_t c = 0; c < NC; ++c)
        {
            double v = Mxn[c] * i;
            for (size_t d = 0; d < NC; ++d) v += Mxx[c][d] * h[d];
            for (size_t u = 0; u < NU; ++u) v += Mxu[c][u] * in[u];
            next[c] = v;
        }
        h = next;
    }

    /** Iterazioni di Newton dell'ultimo campione (per le misure). */
    int getLastIterations() const noexcept { return lastIterations; }

private:
    struct Resistor  { int a, b; double ohms; };
    struct Capacitor { int a, b; double farads; };
    struct OpAmp     { int inv, nonInv, out; };

    double& at (std::vector<double>& m, int r, int c)             { return m[(size_t) (r * size + c)]; }
    double at (const std::vector<double>& m, int r, int c) const  { return m[(size_t) (r * size + c)]; }

    /** P = S^-1 con Gauss-Jordan e pivot parziale (matrici piccole: 10-20 righe). */
    void invert()
    {
        std::fill (P.begin(), P.end(), 0.0);
        for (int r = 0; r < size; ++r)
            at (P, r, r) = 1.0;

        for (int col = 0; col < size; ++col)
        {
            int piv = col;
            for (int r = col + 1; r < size; ++r)
                if (std::abs (at (S, r, col)) > std::abs (at (S, piv, col)))
                    piv = r;

            if (piv != col)
                for (int c = 0; c < size; ++c)
                {
                    std::swap (at (S, col, c), at (S, piv, c));
                    std::swap (at (P, col, c), at (P, piv, c));
                }

            const double d = at (S, col, col);
            jassert (std::abs (d) > 1e-300);   // nodo flottante: circuito descritto male
            const double inv = 1.0 / d;
            for (int c = 0; c < size; ++c)
            {
                at (S, col, c) *= inv;
                at (P, col, c) *= inv;
            }

            for (int r = 0; r < size; ++r)
            {
                if (r == col)
                    continue;
                const double f = at (S, r, col);
                if (juce::exactlyEqual (f, 0.0))
                    continue;
                for (int c = 0; c < size; ++c)
                {
                    at (S, r, c) -= f * at (S, col, c);
                    at (P, r, c) -= f * at (P, col, c);
                }
            }
        }
    }

    /** Tensione sulla coppia di diodi: v = p + K i(v), con i(v) = -2 Is sinh(v / nVt) (corrente
        che entra nel nodo). g(v) = v - p + 2 K Is sinh(v / nVt) è crescente: Newton dentro un
        intervallo che contiene sempre la radice, con bisezione se un passo ne esce. */
    double solveDiode (double p) noexcept
    {
        const double sign = p >= 0.0 ? 1.0 : -1.0;
        const double a = std::abs (p), twoKIs = 2.0 * K * diodeIs;
        double lo = 0.0, hi = std::min (a, 1.5);   // la radice ha il segno di p e |v| < |p|

        // stima iniziale: soluzione del campione prima spostata lungo la pendenza dv/dp di allora
        double v = juce::jlimit (lo, hi, sign * (vDiode + (p - pLast) * slopeLast));
        double e = std::exp (v / diodeVt), ie = 1.0 / e, dg = 1.0;

        lastIterations = 0;
        for (int it = 0; it < 40; ++it)
        {
            ++lastIterations;
            const double g = v - a + twoKIs * 0.5 * (e - ie);
            dg = 1.0 + twoKIs * 0.5 * (e + ie) / diodeVt;

            if (g > 0.0) hi = v; else lo = v;

            double next = v - g / dg;
            if (next <= lo || next >= hi)
                next = 0.5 * (lo + hi);

            const double step = next - v;
            v = next;
            e = std::exp (v / diodeVt);
            ie = 1.0 / e;
            if (std::abs (step) < 1e-6)   // 1 µV: sotto -100 dB rispetto ai segnali sui diodi
                break;
        }

        vDiode = sign * v;
        pLast = p;
        slopeLast = 1.0 / dg;
        return -sign * diodeIs * (e - ie);   // -2 Is sinh(v / nVt), con il segno di p
    }

    std::vector<Resistor> resistors;
    std::vector<Capacitor> capacitors;
    std::vector<int> inputs, outputs;
    std::vector<OpAmp> opAmps;
    int numNodes = 0, size = 0;
    int diodeNode = 0;
    double diodeIs = 0.0, diodeVt = 1.0, K = 0.0;
    bool dirty = true;

    std::vector<double> S, P;   // solo per update()
    std::array<std::array<double, NC>, NC> Mxx {};
    std::array<std::array<double, NU>, NC> Mxu {};
    std::array<double, NC> Mxn {}, Mnx {}, gc {}, h {};
    std::array<double, NU> Mnu {};
    std::array<std::array<double, NC>, NY> Myx {};
    std::array<std::array<double, NU>, NY> Myu {};
    std::array<double, NY> Myn {};
    double vDiode = 0.0, pLast = 0.0, slopeLast = 1.0;
    int lastIterations = 0;
};
