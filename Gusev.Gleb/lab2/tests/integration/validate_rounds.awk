BEGIN { word["alive"] = "жив"; word["dead"] = "мёртв" }

function abs(x) { return x < 0 ? -x : x }
function bad(msg) { print "НАРУШЕНИЕ: раунд " round ": " msg; violations++ }

/Игра началась/ { delete alive; lastRound = 0; next }

/INFO : раунд [0-9]+:/ {
    rounds++
    n = split($0, part, / \| /)
    header = part[1]
    summary = part[n]
    kids = n - 2

    match(header, /раунд [0-9]+/);           round = substr(header, RSTART + 11, RLENGTH - 11) + 0
    match(header, /волк [0-9]+/);            wolf = substr(header, RSTART + 9, RLENGTH - 9) + 0
    match(header, /козлят [0-9]+/);          declared = substr(header, RSTART + 13, RLENGTH - 13) + 0
    match(header, /укрытия [0-9.]+/);        hide = substr(header, RSTART + 15, RLENGTH - 15)
    match(header, /воскрешения [0-9.]+/);    res = substr(header, RSTART + 23, RLENGTH - 23)

    if (lastRound && round != lastRound + 1) bad("номер раунда " round " после " lastRound)
    lastRound = round
    if (wolf < 1 || wolf > 100) bad("число волка " wolf " вне 1..100")
    if (declared != kids) bad("заявлено козлят " declared ", в строке " kids)
    if (kids <= 0) { bad("в раунде нет козлят"); next }
    if (hide != sprintf("%.2f", 70 / kids)) bad("порог укрытия " hide ", ожидалось " sprintf("%.2f", 70 / kids))
    if (res != sprintf("%.2f", 20 / kids)) bad("порог воскрешения " res ", ожидалось " sprintf("%.2f", 20 / kids))

    hidden = caught = resurrected = deadAfter = 0
    for (i = 2; i < n; i++) {
        seg = part[i]
        split(seg, w, " ")
        id = w[1]; number = w[2] + 0
        split(seg, arrow, "→")
        before = (index(seg, " жив→") > 0) ? "alive" : "dead"
        outcome = arrow[2]
        throws++

        if (id in alive) {
            if (before != alive[id]) bad(id " до хода " word[before] ", а после прошлого хода был " word[alive[id]])
        } else if (before != "alive") {
            bad(id " впервые в игре, но не жив")
        }

        diff = abs(number - wolf)
        if (before == "alive") {
            if (number < 1 || number > 100) bad(id " живой бросил " number " (нужно 1..100)")
            expect = (diff <= 70 / kids) ? "спрятался" : "попался"
        } else {
            if (number < 1 || number > 50) bad(id " мёртвый бросил " number " (нужно 1..50)")
            expect = (diff <= 20 / kids) ? "воскрес" : "остался мёртв"
        }
        if (outcome != expect) bad(id " бросил " number " при волке " wolf " (разница " diff "): " outcome ", ожидалось " expect)

        if (outcome == "спрятался") hidden++
        if (outcome == "попался") caught++
        if (outcome == "воскрес") resurrected++
        after = (outcome == "спрятался" || outcome == "воскрес") ? "alive" : "dead"
        if (after == "dead") deadAfter++
        alive[id] = after
    }

    expectSummary = "спрятались " hidden ", попались " caught ", воскресли " resurrected ", мертвы " deadAfter
    if (summary != expectSummary) bad("итоги «" summary "», по исходам «" expectSummary "»")
}

END { print "проверено раундов " rounds + 0 ", бросков " throws + 0 ", нарушений " violations + 0 }
