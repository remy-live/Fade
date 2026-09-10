# -*- coding: utf-8 -*-
"""Le manuel du chanteur, en français, en PDF.

Le README de ce dossier explique POURQUOI chaque chose est faite ainsi, en
anglais, sur quarante pages : c'est le document de celui qui touche au
code. Celui-ci est l'autre : ce qu'il faut faire, dans l'ordre, sans un
mot de vocabulaire technique, pour quelqu'un qui a un micro dans les
mains. Les deux sont écrits à la main - rien ici n'est engendré.

    python3 make_manual.py
"""

from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont as TTF
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame,
                                Paragraph, Spacer, Table, TableStyle,
                                PageBreak)

# The built-in fonts carry Latin-1 and nothing else: every arrow of the
# interface - USER (arrow), GO TO (arrow), the delete cross - came out a
# solid black box, which is worse than not drawing it. DejaVu has them
# all, and embeds, so the file reads the same on any machine. There is no
# proportional italic in it, so the asides are set in grey instead.
_D = "/usr/share/fonts/truetype/dejavu/"
pdfmetrics.registerFont(TTF("DJV", _D + "DejaVuSans.ttf"))
pdfmetrics.registerFont(TTF("DJV-B", _D + "DejaVuSans-Bold.ttf"))
pdfmetrics.registerFontFamily("DJV", normal="DJV", bold="DJV-B",
                              italic="DJV", boldItalic="DJV-B")

ENCRE   = colors.HexColor("#12181f")
GRIS    = colors.HexColor("#5d6b79")
VERT    = colors.HexColor("#1d8f58")
AMBRE   = colors.HexColor("#9a6c00")
TRAIT   = colors.HexColor("#d5dde4")
FOND    = colors.HexColor("#f2f5f8")

TITRE = ParagraphStyle("titre", fontName="DJV-B", fontSize=26,
                       leading=30, textColor=ENCRE, spaceAfter=2)
SOUS  = ParagraphStyle("sous", fontName="DJV", fontSize=11,
                       leading=15, textColor=GRIS, spaceAfter=16)
H     = ParagraphStyle("h", fontName="DJV-B", fontSize=15,
                       leading=18, textColor=ENCRE,
                       spaceBefore=16, spaceAfter=6)
H2    = ParagraphStyle("h2", fontName="DJV-B", fontSize=11,
                       leading=14, textColor=VERT,
                       spaceBefore=10, spaceAfter=3)
P     = ParagraphStyle("p", fontName="DJV", fontSize=10,
                       leading=14.5, textColor=ENCRE, spaceAfter=6)
PP    = ParagraphStyle("pp", parent=P, leftIndent=10)
NOTE  = ParagraphStyle("note", fontName="DJV", fontSize=9.5,
                       leading=13.5, textColor=GRIS, spaceAfter=6)
CELL  = ParagraphStyle("cell", fontName="DJV", fontSize=9.5,
                       leading=13, textColor=ENCRE)
CELLG = ParagraphStyle("cellg", parent=CELL, fontName="DJV-B")


def tableau(lignes, larg=(52*mm, 108*mm), tete=None):
    donnees = []
    style = [
        ("VALIGN",       (0, 0), (-1, -1), "TOP"),
        ("LINEBELOW",    (0, 0), (-1, -2), 0.4, TRAIT),
        ("TOPPADDING",   (0, 0), (-1, -1), 5),
        ("BOTTOMPADDING",(0, 0), (-1, -1), 5),
        ("LEFTPADDING",  (0, 0), (-1, -1), 0),
        ("RIGHTPADDING", (0, 0), (-1, -1), 8),
    ]
    if tete:
        donnees.append([Paragraph('<font color="#5d6b79">%s</font>' % c, CELLG)
                        for c in tete])
        style += [("LINEBELOW", (0, 0), (-1, 0), 0.8, GRIS),
                  ("BOTTOMPADDING", (0, 0), (-1, 0), 6)]
    for a, b in lignes:
        donnees.append([Paragraph(a, CELLG), Paragraph(b, CELL)])
    t = Table(donnees, colWidths=list(larg))
    t.setStyle(TableStyle(style))
    return t


def encadre(titre, lignes, couleur=VERT):
    dedans = [Paragraph('<font color="#%s">%s</font>' % (couleur.hexval()[2:],
                                                         titre), H2)]
    for l in lignes:
        dedans.append(Paragraph(l, P))
    t = Table([[dedans]], colWidths=[160*mm])
    t.setStyle(TableStyle([
        ("BACKGROUND",   (0, 0), (-1, -1), FOND),
        ("BOX",          (0, 0), (-1, -1), 0.6, TRAIT),
        ("LEFTPADDING",  (0, 0), (-1, -1), 10),
        ("RIGHTPADDING", (0, 0), (-1, -1), 10),
        ("TOPPADDING",   (0, 0), (-1, -1), 2),
        ("BOTTOMPADDING",(0, 0), (-1, -1), 8),
    ]))
    return t


def pied(canevas, doc):
    canevas.saveState()
    canevas.setFont("DJV", 8)
    canevas.setFillColor(GRIS)
    canevas.drawString(25*mm, 14*mm, "VOICE — manuel rapide")
    canevas.drawRightString(185*mm, 14*mm, "%d" % doc.page)
    canevas.setStrokeColor(TRAIT)
    canevas.setLineWidth(0.4)
    canevas.line(25*mm, 18*mm, 185*mm, 18*mm)
    canevas.restoreState()


histoire = []
A = histoire.append

# ---------------------------------------------------------------- page 1
A(Paragraph("VOICE", TITRE))
A(Paragraph("Le manuel rapide. Tout ce qu'il faut savoir, rien d'autre.",
            SOUS))

A(Paragraph("Ce que c'est", H))
A(Paragraph(
    "Une tranche de voix complète dans un seul bloc : porte, "
    "compresseur, dé-esseur, EQ, drive, pitch, harmonies, doubleur, "
    "chorus, anti-Larsen, delay, réverbe. Le son passe dans tout ça "
    "dans cet ordre.", P))
A(Paragraph(
    "Vous avez <b>72 sons tout faits</b> et <b>6 favoris à vous</b>, "
    "que vous remplissez, nommez et rappelez au pied. C'est tout le sujet "
    "de ce manuel.", P))

A(Paragraph("Vos 6 favoris", H))
A(tableau([
    ("Je veux garder<br/>le son que j'entends",
     "Choisissez le slot avec <b>SAVE TO</b> (USER 1 à 6), puis "
     "<b>SAVE</b>. C'est ce que vous entendez qui est gardé, pas ce "
     "qu'il y avait avant."),
    ("Je veux le rappeler",
     "Dans la liste <b>PROGRAM</b>, ou dans <b>FAVORIS</b>, ou au pied "
     "(voir plus bas)."),
    ("Je veux le nommer",
     "Ouvrez les réglages du bloc : six cases, <b>7 lettres</b> "
     "chacune. Le nom part tout seul pendant que vous tapez. Rien à "
     "valider, et <b>SAVE ne renomme jamais</b>."),
    ("Je veux le jeter",
     "La croix <b>&#10005;</b> à côté du favori. Elle "
     "devient rouge et dit <b>SÛR ?</b> : recliquez dans les trois "
     "secondes. Il n'y a pas de retour en arrière."),
    ("Je veux le modifier",
     "Rappelez-le, tournez ce que vous voulez — le bouton "
     "touché vous revient — puis <b>SAVE</b> au même "
     "endroit."),
], tete=("Je veux…", "Je fais…")))

A(Paragraph("Un slot vide vous laisse les commandes : c'est l'état "
            "dans lequel on règle un son avant de l'enregistrer.", NOTE))

A(Spacer(1, 6*mm))
A(encadre("Si vous ne lisez que ça", [
    "<b>1.</b> Réglez un son qui vous plaît. <b>SAVE TO</b> sur "
    "USER 1, puis <b>SAVE</b>.",
    "<b>2.</b> Recommencez pour USER 2 et USER 3 — un son par "
    "moment de la chanson.",
    "<b>3.</b> Nommez-les : ouvrez les réglages, tapez dans les six "
    "cases. Le nom part tout seul.",
    "<b>4.</b> Mettez <b>USER ▶</b> sur un footswitch. Il tourne entre "
    "vos favoris, et le switch porte leur nom.",
    "<b>5.</b> Aux balances : réglez ce que la salle demande, allumez "
    "le <b>cadenas</b> de cette section, et vos favoris partagent ce "
    "réglage pour la soirée sans que rien soit modifié pour de bon.",
]))

# ---------------------------------------------------------------- page 2
A(PageBreak())
A(Paragraph("Au pied", H))
A(Paragraph(
    "Chaque commande peut aller sur un footswitch du Dwarf. Les plus "
    "utiles :", P))
A(tableau([
    ("USER ▶",
     "Le favori suivant qui a quelque chose dedans. C'est le switch du "
     "cycle : appuyez, vous avancez, et ça tourne en rond. Les slots "
     "vides sont sautés."),
    ("GO TO ▷",
     "Pour aller chercher le 3<super>e</super> sans jouer le 2<super>e</super>. Chaque appui "
     "<b>déplace un curseur sans changer le son</b>, et le switch dit "
     "le nom où vous êtes rendu. Quand c'est le bon : "
     "<b>maintenez une demi-seconde</b> et vous y allez."),
    ("GO 1 … GO 6",
     "Un switch par favori. Un appui, on y est, toujours, quoi qu'il se "
     "soit passé avant. Chaque switch porte le nom de son favori."),
    ("FX",
     "Coupe les quatre effets d'un coup, en douceur — les queues de "
     "delay et de réverbe finissent de sonner."),
    ("MUTE",
     "Coupe le son. Entre deux chansons."),
    ("TAP",
     "Deux appuis donnent le tempo du delay."),
    ("A/B",
     "Retour au son précédent, et re-appuyez pour revenir. "
     "Pour comparer deux sons aux balances."),
]))

A(Paragraph("CYCLE", H))
A(Paragraph(
    "Un réglage, de <b>1 à 6</b>. Il dit jusqu'où vont "
    "<b>USER ▶</b> et <b>GO TO</b>. Si vous n'avez besoin que de trois "
    "sons ce soir, mettez-le sur 3 : le cycle ne passera plus par les "
    "trois autres.", P))
A(Paragraph(
    "Les switches <b>GO 1 à GO 6</b> l'ignorent complètement : un "
    "switch qui porte le nom d'un favori doit toujours pouvoir l'atteindre.",
    P))
A(Paragraph("À mettre sur un encodeur : c'est la set list qui "
            "décide ça, pas le matériel.", NOTE))

# ---------------------------------------------------------------- page 3
A(PageBreak())
A(Paragraph("Les cadenas — la salle contre la set list", H))
A(Paragraph(
    "C'est la fonction la moins évidente et la plus utile. Elle "
    "répond à une seule question :", P))
A(Paragraph(
    "<b>« Quand je change de favori, est-ce que ce bloc-là "
    "bouge ? »</b>", P))
A(Paragraph(
    "Sans elle : la salle est un tonneau, vous baissez la réverbe sur "
    "votre favori CHORUS, vous passez à SOLO — et SOLO ramène "
    "<b>sa</b> réverbe. Vous re-réglez. Toute la soirée.", P))
A(Paragraph(
    "Avec elle : vous réglez la réverbe une fois pour la salle, "
    "vous allumez le cadenas de la section RÉVERBE, et "
    "<b>plus aucun favori ni son d'usine n'y touche</b>.", P))

A(Spacer(1, 3*mm))
A(encadre("Aux balances, en pratique", [
    "<b>1.</b> Allez sur le favori qui vous sert de base.",
    "<b>2.</b> Réglez le bloc jusqu'à ce que ce soit bon pour "
    "la salle — la compression, par exemple.",
    "<b>3.</b> Allumez le cadenas de cette section.",
    "<b>4.</b> Passez d'un favori à l'autre : chacun garde son drive, "
    "son octave, sa réverbe… <b>et tous partagent votre "
    "compression</b>.",
    "<b>5.</b> À la fin de la soirée, éteignez le cadenas. "
    "Chaque favori retrouve la sienne. Vous n'avez rien abîmé.",
]))

A(Spacer(1, 3*mm))
A(Paragraph("Trois choses à savoir", H2))
A(Paragraph(
    "• Ce qui est verrouillé, c'est le <b>réglage</b>, pas "
    "l'interrupteur du bloc. Si un favori a été enregistré "
    "avec la réverbe <b>éteinte</b>, elle reste éteinte. "
    "Sinon une réverbe verrouillée s'allumerait dans les sons "
    "bâtis sans réverbe.", PP))
A(Paragraph(
    "• <b>SAVE ne touche pas à un bloc verrouillé</b> dans un "
    "slot qui a déjà quelque chose dedans. C'est exprès : "
    "sinon la salle de ce soir se graverait dans vos six favoris. Un slot "
    "encore <b>vide</b>, lui, prend tout — c'est la façon propre "
    "de capturer « la version de ce soir ».", PP))
A(Paragraph(
    "• Éteindre un cadenas <b>rend le bloc immédiatement</b> "
    "au favori en cours. Les boutons bougent sous vos yeux.", PP))

A(Spacer(1, 3*mm))
A(Paragraph("Où ils sont", H2))
A(tableau([
    ("Sur l'écran de l'ordi",
     "Un petit <b>cadenas ambre</b> dans le coin de chaque section, à "
     "côté de l'interrupteur du bloc."),
    ("Dans les réglages",
     "Une section à eux, avec l'explication en dessous."),
    ("Sur le Dwarf",
     "Au bout de la marche de l'<b>ENC 2</b>, après les "
     "paramètres. L'<b>ENC 3</b> dit <b>MINE</b> (verrouillé) ou "
     "<b>PROGRAM</b> (libre)."),
]))
A(Paragraph(
    "Et ça se voit : un paramètre verrouillé affiche "
    "<b>MINE</b> à la place de son unité, et l'<b>ENC 1</b> porte "
    "un <b>LOCK 2</b> permanent à côté du nom du favori. Un "
    "cadenas oublié la semaine dernière, c'est un favori qui "
    "sonne faux sans raison visible.", NOTE))

# ---------------------------------------------------------------- page 4
A(PageBreak())
A(Paragraph("Une page du Dwarf, et toute la boucle", H))
A(Paragraph(
    "Trois encodeurs sur une page, sans ordinateur :", P))
A(tableau([
    ("ENC 1 — FAVORI",
     "Fait défiler vos six favoris, par leur nom. Le son suit pendant "
     "que vous tournez."),
    ("ENC 2 — PARAMÈTRE",
     "Fait défiler <b>NOM</b> d'abord, puis tous les réglages du "
     "son, puis les douze cadenas."),
    ("ENC 3 — VALEUR",
     "Change celui que l'ENC 2 montre. Sur <b>NOM</b>, il ouvre "
     "l'éditeur de nom."),
]))
A(Spacer(1, 2*mm))
A(encadre("La salle est plus mate qu'à la balance", [
    "ENC 2 sur PRESENCE, ENC 3 deux crans vers le haut, ENC 2 sur NOM, "
    "ENC 3 pour ouvrir, tapez <b>SALLE B</b>, ENC 3 à droite pour "
    "garder, puis <b>SAVE</b>. Trois chansons plus tard, vous revenez au "
    "pied et vous changez la réverbe. Sans ordinateur, sans "
    "navigateur, sur une page.",
]))
A(Paragraph(
    "<b>Taper un nom.</b> L'ENC 3 sur NOM ouvre l'éditeur, dans "
    "n'importe quel sens. Ensuite : <b>ENC 1</b> déplace le curseur "
    "dans le mot (la lettre en cours clignote), <b>ENC 2</b> choisit la "
    "lettre, <b>ENC 3</b> répond — <b>à gauche non, à "
    "droite oui</b>. Quinze secondes sans rien toucher et il abandonne "
    "tout seul : un silence n'est pas une décision.", P))

A(Paragraph("Si quelque chose ne répond pas", H))
A(Paragraph(
    "En haut à gauche du bloc, il y a une ligne de texte. C'est le "
    "plugin qui dit ce qu'il voit — lisez-la avant de chercher plus "
    "loin. Elle passe en <b>ambre</b> quand quelque chose ne va pas.", P))
A(tableau([
    ("« ENLEVER LE BLOC DU<br/>PEDALBOARD ET LE<br/>REMETTRE »",
     "Une mise à jour a ajouté des commandes et votre bloc est "
     "plus vieux qu'elles. <b>Enlevez le bloc VOICE du pedalboard et "
     "remettez-le.</b> À faire après chaque installation qui "
     "ajoute des ports."),
    ("« GO TO JAMAIS VU<br/>BOUGER »",
     "Le switch n'est pas connecté : même remède."),
    ("« AUCUN MAINTIEN VU »",
     "Le maintien de GO TO ne peut pas fonctionner : dans l'adressage du "
     "footswitch, il faut <b>Momentary On</b>."),
    ("« 0 favoris<br/>enregistres »",
     "Vous avez peut-être <i>nommé</i> des slots sans faire "
     "<b>SAVE</b>. Un nom n'est pas un son."),
]))
A(Spacer(1, 4*mm))
A(Paragraph(
    "Le mode d'emploi complet — chaque bouton, et pourquoi chaque "
    "chose est faite ainsi — est dans le README des sources.", NOTE))


doc = BaseDocTemplate("VOICE-manuel.pdf", pagesize=A4,
                      title="VOICE — manuel rapide",
                      author="REMY", subject="MOD Dwarf")
cadre = Frame(25*mm, 22*mm, 160*mm, 252*mm, id="c",
              leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0)
doc.addPageTemplates([PageTemplate(id="p", frames=[cadre], onPage=pied)])
doc.build(histoire)
print("VOICE-manuel.pdf")
